#include <Arduino.h>
#include <TinyGPS++.h>
#include <RF24.h>

#include "link.h"
#include "pins.h"
#include "protocol.h"

// Initialize the RF24 radio with the specified CE and CSN pins
RF24 radio(PIN_NRF_CE, PIN_NRF_CSN);

// Radio settings; the drone's radio module must use the same ones.
static const uint8_t RADIO_CHANNEL = 76;
static const uint8_t RADIO_ADDRESS[5] = {'D', 'R', 'N', '0', '1'};

// Send a test CONTROL packet at 50 Hz
static const uint32_t CONTROL_INTERVAL_MS = 20;
uint32_t lastControlMs = 0;
bool radioOk = false;
bool linkOk = false;

// Link counters, printed and reset once per second
struct LinkStats {
  uint32_t sent;          // packets handed to the radio
  uint32_t acked;         // radio got an ACK from the drone
  uint32_t ackPayloads;   // ACKs that carried telemetry
  uint32_t verifyOk;      // telemetry passed authentication
  uint32_t verifyTag;     // bad tag (wrong key / forged / corrupted)
  uint32_t verifyReplay;  // counter not newer than the last one
  uint32_t verifyLength;  // bad length
  uint32_t signFail;      // couldn't sign (counter exhausted)
};
LinkStats linkStats = {};

// GPS on hardware UART0 (Serial1):
// GPS TX -> GP1 (PIN_GPS_RX), GPS RX -> GP0 (PIN_GPS_TX)
HardwareSerial &gpsSerial = Serial1;

// Default baud rate for most common GPS modules (like NEO-6M) is 9600
static const uint32_t GPSBaud = 9600;

TinyGPSPlus gps;

// Echo raw NMEA from the GPS to the Serial Monitor. $GPGSV lines list the
// satellites in view as (PRN, elevation, azimuth, SNR dB); an empty SNR means
// the satellite is known but not being tracked. Send 'r' to toggle.
bool rawNmea = true;

// How often to print the parsed status line
static const uint32_t STATUS_INTERVAL_MS = 1000;
uint32_t lastStatusMs = 0;

void displayGPSInfo();
void sendControl();
void printLinkStats();

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);

  // Start USB serial for the PC Serial Monitor
  Serial.begin(115200);

  // Start hardware serial communications with the GPS module
  Serial1.setTX(PIN_GPS_TX);
  Serial1.setRX(PIN_GPS_RX);
  gpsSerial.begin(GPSBaud);

  Serial.println(F("TinyGPS++ Initialization Complete"));
  Serial.println(F("Send 'r' to toggle raw NMEA output"));

  linkOk = link_begin();
  if (!linkOk) {
    Serial.println(F("Error: link counter unavailable. Generate a new link key."));
  }

  // Controller transmits; the drone replies with telemetry in ACK payloads
  radioOk = radio.begin();
  if (radioOk) {
    radio.setDataRate(RF24_250KBPS);   // best range on PA/LNA modules
    radio.setPALevel(RF24_PA_LOW);     // keep low on the bench
    radio.setChannel(RADIO_CHANNEL);
    radio.setRetries(1, 3);            // 500 us between retries, 3 retries
    radio.setAddressWidth(5);
    radio.enableDynamicPayloads();
    radio.enableAckPayload();
    radio.openWritingPipe(RADIO_ADDRESS);
    radio.stopListening();
    Serial.println(F("NRF24 Initialization Complete"));
  } else {
    Serial.println(F("Error: NRF24 not responding. Check wiring and power."));
  }
}

void loop() {
  // Toggle raw NMEA passthrough from the Serial Monitor
  while (Serial.available() > 0) {
    if (Serial.read() == 'r') {
      rawNmea = !rawNmea;
      Serial.println(rawNmea ? F("Raw NMEA: on") : F("Raw NMEA: off"));
    }
  }

  // Continuously feed data from the GPS module into the TinyGPS++ library
  static bool atLineStart = true;
  while (gpsSerial.available() > 0) {
    char c = gpsSerial.read();
    if (rawNmea) {
      Serial.write(c);
    }
    atLineStart = (c == '\n');
    gps.encode(c);
  }

  if (radioOk && linkOk && millis() - lastControlMs >= CONTROL_INTERVAL_MS) {
    lastControlMs = millis();
    sendControl();
  }

  // Print the status once per second. Wait for the end of a raw NMEA line so
  // the two don't interleave.
  if ((!rawNmea || atLineStart) &&
      millis() - lastStatusMs >= STATUS_INTERVAL_MS) {
    lastStatusMs = millis();
    if (gps.charsProcessed() > 0) {
      displayGPSInfo();
    }
    printLinkStats();
  }

  // Debugging fallback: If no data arrives within 5 seconds, check wiring.
  // Repeats every 5 s without blocking, so the radio keeps sending.
  static uint32_t lastGpsWarnMs = 0;
  if (millis() > 5000 && gps.charsProcessed() < 10 &&
      millis() - lastGpsWarnMs >= 5000) {
    lastGpsWarnMs = millis();
    Serial.println(F("Error: No GPS data detected. Check your wiring and baud rate!"));
  }
}

void displayGPSInfo() {
  Serial.print(F("Location: ")); 
  if (gps.location.isValid()) {
    // Latitude and longitude require a high floating-point precision (6 decimals)
    Serial.print(gps.location.lat(), 6);
    Serial.print(F(", "));
    Serial.print(gps.location.lng(), 6);
  } else {
    Serial.print(F("INVALID (Waiting for satellite fix...)"));
  }

  Serial.print(F("  Date/Time: "));
  if (gps.date.isValid() && gps.time.isValid()) {
    Serial.print(gps.date.month());
    Serial.print(F("/"));
    Serial.print(gps.date.day());
    Serial.print(F("/"));
    Serial.print(gps.date.year());
    Serial.print(F(" "));
    if (gps.time.hour() < 10) Serial.print(F("0"));
    Serial.print(gps.time.hour());
    Serial.print(F(":"));
    if (gps.time.minute() < 10) Serial.print(F("0"));
    Serial.print(gps.time.minute());
  } else {
    Serial.print(F("INVALID"));
  }

  Serial.print(F("  Satellites: "));
  if (gps.satellites.isValid()) {
    Serial.print(gps.satellites.value());
  } else {
    Serial.print(F("INVALID"));
  }

  Serial.println();
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
}

// Sends one signed CONTROL packet and checks any telemetry in the ACK.
void sendControl() {
  // Test values: sticks centered, throttle off, disarmed, no kill
  ControlPayload ctrl = {};
  ctrl.arm = ARM_DISARMED;

  uint8_t packet[LINK_PACKET_MAX];
  const size_t len = link_sign_uplink(MSG_CONTROL, (const uint8_t *)&ctrl,
                                      sizeof(ctrl), packet);
  if (len == 0) {
    linkStats.signFail++;
    return;
  }

  linkStats.sent++;
  if (!radio.write(packet, len)) {
    return;  // no ACK after retries
  }
  linkStats.acked++;

  while (radio.available()) {
    const uint8_t ackLen = radio.getDynamicPayloadSize();
    if (ackLen == 0 || ackLen > LINK_PACKET_MAX) {
      radio.flush_rx();  // corrupt length; drop it
      linkStats.verifyLength++;
      break;
    }
    uint8_t ack[LINK_PACKET_MAX];
    radio.read(ack, ackLen);
    linkStats.ackPayloads++;

    uint8_t msgId;
    const uint8_t *payload;
    size_t payloadLen;
    switch (link_verify_downlink(ack, ackLen, &msgId, &payload, &payloadLen)) {
      case LINK_OK:         linkStats.verifyOk++; break;
      case LINK_ERR_TAG:    linkStats.verifyTag++; break;
      case LINK_ERR_REPLAY: linkStats.verifyReplay++; break;
      case LINK_ERR_LENGTH: linkStats.verifyLength++; break;
    }
  }
}

// Prints and resets the link counters for the last second.
void printLinkStats() {
  if (!radioOk || !linkOk) {
    Serial.println(radioOk ? F("Link: disabled (no counter)")
                           : F("Link: disabled (NRF24 not responding)"));
    return;
  }
  Serial.printf("Link: sent %lu  acked %lu (%lu%%)  telemetry %lu  "
                "ok %lu  bad_tag %lu  replay %lu  bad_len %lu  sign_fail %lu\n",
                linkStats.sent, linkStats.acked,
                linkStats.sent ? linkStats.acked * 100 / linkStats.sent : 0,
                linkStats.ackPayloads, linkStats.verifyOk, linkStats.verifyTag,
                linkStats.verifyReplay, linkStats.verifyLength,
                linkStats.signFail);
  linkStats = {};
}
