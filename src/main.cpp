#include <Arduino.h>

// On the Pico Plus 2 W the onboard LED is wired to the CYW43439, not an RP2350 GPIO;
// the core routes LED_BUILTIN through the wireless chip.
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
}
