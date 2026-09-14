// Central configuration for the NanoH2 pushbutton indicator.
// Everything a user is expected to tune lives in this file.

#pragma once

#include <Arduino.h>

/* ------------------------------------------------------------------
 * Pins - M5Stack NanoH2 (SKU C149)
 *
 * The Grove HY2.0-4P port carries GND (black), 5V (red), G2 (yellow)
 * and G1 (white). The button sits on G1 (white); change PIN_BUTTON to
 * move it to G2.
 * ------------------------------------------------------------------ */
#define PIN_BUTTON    2   // Grove white / G2 - pushbutton, feeds 3.3 V when closed
#define PIN_RGB      11   // on-board WS2812 data
#define PIN_RGB_POWER 10  // on-board WS2812 power enable, HIGH = LED powered
#define PIN_LED_BLUE  4   // on-board blue LED, unused here

// The button feeds 3.3 V into G2 when closed, so a closed contact reads HIGH
// and the internal pull-down holds the pin low while the contact is open.
// Set this to 0 for the other common wiring, a button that opens to GND.
#define BUTTON_ACTIVE_HIGH 1

// The raw level has to hold this long before it is accepted, which swallows
// contact bounce on both close and release.
#define BUTTON_DEBOUNCE_MS 20

/* ------------------------------------------------------------------
 * LED
 *
 * Values are plain 0..255 RGB; rgbLedWrite() applies the WS2812 GRB
 * ordering. Keep them low, the on-board LED is bright and close.
 * ------------------------------------------------------------------ */
struct LedColor {
  uint8_t r, g, b;
};

static const LedColor COLOR_OFF = {0, 0, 0};
static const LedColor COLOR_SIGNAL = {0, 40, 0};      // green:  3.3 V seen on PIN_BUTTON
static const LedColor COLOR_NO_SIGNAL = {40, 30, 0};  // yellow: contact open
