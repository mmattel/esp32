/**
 * M5Stack NanoH2 (ESP32-H2, SKU C149) - pushbutton shown on the RGB LED.
 *
 * - A pushbutton on PIN_BUTTON (G1) feeds 3.3 V into the pin when closed. The
 *   pin's internal pull-down holds it low while the contact is open.
 * - The on-board RGB LED is green while that signal is present and yellow while
 *   it is absent. There is no other state: the LED always shows one of the two.
 * - No Zigbee, no radio, no NVS. Everything happens in loop().
 *
 * Arduino IDE settings:
 *   Board            M5NanoH2  (or ESP32H2 Dev Module)
 *   Zigbee mode      Disabled
 *   Partition Scheme Default 4MB with spiffs
 *   USB CDC On Boot  Enabled   (for the USB-C serial console)
 *
 * To flash: hold the on-board G9 button, then plug in USB-C.
 *
 * See README.md for wiring and where the 3.3 V comes from.
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
  return BUTTON_ACTIVE_LOW ? (level == LOW) : (level == HIGH);
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
  pinMode(PIN_BUTTON, BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
}

void loop() {
  static bool lastLogged = false;
  static bool everLogged = false;

  bool signal = signalPresent();

  if (!everLogged || signal != lastLogged) {
    Serial.printf("G%d %s -> %s\r\n", PIN_BUTTON, signal ? "3.3 V" : "open",
                  signal ? "green" : "yellow");
    lastLogged = signal;
    everLogged = true;
  }

  ledWrite(signal ? COLOR_SIGNAL : COLOR_NO_SIGNAL);
  delay(10);
}
