// Central configuration for the NanoH2 pushbutton indicator.
// Everything a user is expected to tune lives in this file.

#pragma once

#include <Arduino.h>

/* ------------------------------------------------------------------
 * Pins - M5Stack NanoH2 (SKU C149)
 *
 * The Grove HY2.0-4P port carries GND (black), 5V (red), G2 (yellow)
 * and G1 (white). The button sits on G2 (yellow); change PIN_BUTTON to
 * move it to G1.
 *
 * PIN_BUTTON 9 uses the on-board button instead, with nothing wired at
 * all. The code is the same - only the expectations change:
 *
 * - G9 is the boot strapping pin, so holding it while the board powers
 *   up enters ROM download mode and the sketch does not run. Press it
 *   once the sketch is up and the LED follows it as usual.
 * - The Grove port is then unused, which rather defeats the point of
 *   this sketch: it exists to prove out an external button before that
 *   button is trusted in the Zigbee sketch. Handy as a check that the
 *   on-board one works, though.
 *
 * The cable colours are the board's silkscreen, not a guarantee about
 * the cable in your hand - see README.md.
 * ------------------------------------------------------------------ */
#define PIN_BUTTON    2   // Grove yellow / G2 - pushbutton, pulls the pin to GND when closed
#define PIN_RGB      11   // on-board WS2812 data
#define PIN_RGB_POWER 10  // on-board WS2812 power enable, HIGH = LED powered
#define PIN_LED_BLUE  4   // on-board blue LED, unused here

// The button pulls its pin down to GND when closed, so a closed contact reads LOW
// and the pin is held high while the contact is open - by the internal pull-up
// this switches on, and on a button breakout by its own pull-up resistor too.
// Set this to 1 for the other wiring, a button that feeds 3.3 V into the pin.
#define BUTTON_ACTIVE_HIGH 0

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
static const LedColor COLOR_SIGNAL = {0, 40, 0};      // green:  contact closed on PIN_BUTTON
static const LedColor COLOR_NO_SIGNAL = {40, 30, 0};  // yellow: contact open
