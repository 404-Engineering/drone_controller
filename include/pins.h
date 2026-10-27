#pragma once

// Pin map for the Drone Controller board (Pico 2 W). GPIO numbers, not header
// pins. See docs/electrical/wiring.md.

// GPS on UART0 (Serial1); these are the core's default Serial1 pins.
#define PIN_GPS_TX   0   // Pico TX -> GPS RX
#define PIN_GPS_RX   1   // GPS TX -> Pico RX
#define PIN_GPS_PPS  2

// NRF24L01+ PA/LNA on SPI0 (SPI); these are the core's default SPI pins.
#define PIN_NRF_MISO 16
#define PIN_NRF_CSN  17
#define PIN_NRF_SCK  18
#define PIN_NRF_MOSI 19
#define PIN_NRF_CE   20
#define PIN_NRF_IRQ  21

// Optional kill button to GND (use INPUT_PULLUP).
#define PIN_KILL_BTN 22
