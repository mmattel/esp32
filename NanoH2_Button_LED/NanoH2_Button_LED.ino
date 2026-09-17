/**
 * M5Stack NanoH2 (ESP32-H2, SKU C149) - pushbutton shown on the RGB LED.
 *
 * - A pushbutton on PIN_BUTTON pulls the pin down to GND when closed. The pin's
 *   internal pull-up holds it high while the contact is open. That is an external
 *   button on the Grove port (G2 by default), or PIN_BUTTON 9 for the on-board one -
 *   same code, see config.h for what to expect from each.
 * - The on-board RGB LED is green while the contact is closed and yellow while
 *   it is open. There is no other state: the LED always shows one of the two.
 * - No Zigbee, no radio, no NVS. Everything happens in loop().
 *
 * Arduino IDE settings:
 *   Board            M5NanoH2  (or ESP32H2 Dev Module)
 *   Zigbee mode      Disabled
 *   Partition Scheme Default 4MB with spiffs
 *   USB CDC On Boot  Enabled   (for the USB-C serial console)
 *
 * To flash: hold the on-board G9 button, then plug in USB-C. With PIN_BUTTON 9 that
 * is the same button the sketch reads: held during power-up it flashes instead.
 *
 * See README.md for wiring and for the active level.
 */

#include <Arduino.h>

#include "config.h"

/* ------------------------------ LED ------------------------------- */

void ledWrite(const LedColor &c) {
  static LedColor last = {0, 0, 0};
  static bool initialised = false;
  if (initialised && c.r == last.r && c.g == last.g && c.b == last.b) {
    return;
  }
  rgbLedWrite(PIN_RGB, c.r, c.g, c.b);
  last = c;
  initialised = true;
}

void ledBegin() {
  pinMode(PIN_RGB_POWER, OUTPUT);
  digitalWrite(PIN_RGB_POWER, HIGH);  // the WS2812 is unpowered until this is high
  delay(10);
  rgbLedWrite(PIN_RGB, COLOR_OFF.r, COLOR_OFF.g, COLOR_OFF.b);
}

/* ---------------------------- pushbutton -------------------------- */

bool buttonClosed() {
  int level = digitalRead(PIN_BUTTON);
  return BUTTON_ACTIVE_HIGH ? (level == HIGH) : (level == LOW);
}

// Debounced contact state: a new level is only accepted once it has held for
// BUTTON_DEBOUNCE_MS. Starts out "open", so a board that boots with the button
// released shows yellow immediately rather than after the first debounce pass.
bool signalPresent() {
  static bool stable = false;
  static bool candidate = false;
  static uint32_t candidateSinceMs = 0;

  bool now = buttonClosed();
  uint32_t ms = millis();

  if (now != candidate) {
    candidate = now;
    candidateSinceMs = ms;
  } else if (now != stable && (ms - candidateSinceMs) >= BUTTON_DEBOUNCE_MS) {
    stable = now;
  }
  return stable;
}

/* ------------------------- Arduino entry -------------------------- */

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\r\nM5Stack NanoH2 - pushbutton on the RGB LED");

  ledBegin();
  // Pull the pin to the level the open contact should read, so a disconnected
  // or open button is a defined state rather than a floating one.
  pinMode(PIN_BUTTON, BUTTON_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
}

void loop() {
  static bool lastLogged = false;
  static bool everLogged = false;

  bool signal = signalPresent();

  if (!everLogged || signal != lastLogged) {
    Serial.printf("G%d %s -> %s\r\n", PIN_BUTTON, signal ? "closed" : "open",
                  signal ? "green" : "yellow");
    lastLogged = signal;
    everLogged = true;
  }

  ledWrite(signal ? COLOR_SIGNAL : COLOR_NO_SIGNAL);
  delay(10);
}
