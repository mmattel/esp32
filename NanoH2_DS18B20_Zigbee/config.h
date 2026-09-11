// Central configuration for the NanoH2 DS18B20 Zigbee sensor.
// Everything a user is expected to tune lives in this file.

#pragma once

#include <Arduino.h>

/* ------------------------------------------------------------------
 * Pins - M5Stack NanoH2 (SKU C149)
 *
 * The Grove HY2.0-4P port carries GND (black), 5V (red), G2 (yellow)
 * and G1 (white). "First" external pin is taken to be G2 (yellow),
 * "second" is G1 (white); swap the two defines to reverse that.
 * ------------------------------------------------------------------ */
#define PIN_ONEWIRE   2   // Grove yellow / G2 - DS18B20 data line
#define PIN_BUTTON    1   // Grove white  / G1 - pushbutton to GND
#define PIN_RGB      11   // on-board WS2812 data
#define PIN_RGB_POWER 10  // on-board WS2812 power enable, HIGH = LED powered
#define PIN_LED_BLUE  4   // on-board blue LED, unused here

// Pushbutton is open by default and closes to GND when pushed.
#define BUTTON_ACTIVE_LOW 1

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
static const LedColor COLOR_UNCOMMISSIONED = {40, 0, 40};  // magenta: never joined a network
static const LedColor COLOR_CONNECTED = {0, 40, 0};        // green:   joined and on the air
static const LedColor COLOR_LINK_LOST = {40, 30, 0};       // yellow:  joined once, radio lost
static const LedColor COLOR_RESET_ARMED = {40, 0, 0};      // red:     factory-reset hold in progress
static const LedColor COLOR_RESET_DONE = {40, 40, 40};     // white:   factory reset accepted

// Length of one on/off period for the flashing states, and how much of
// that period the LED is lit.
#define LED_FLASH_CYCLE_MS 3000
#define LED_FLASH_DUTY_PCT 50

/* ------------------------------------------------------------------
 * Temperature sensors
 * ------------------------------------------------------------------ */
// Number of DS18B20 slots. One Zigbee endpoint is created per slot,
// whether or not a sensor is actually present, because the endpoint
// list is fixed once the device is commissioned.
#define MAX_DS18B20_SENSORS 3

// How often the bus is read. TEMP_INTERVAL_DEFAULT_S applies on a
// factory-fresh device; a value stored in NVS wins over it, and a value
// written from Zigbee wins over that (and is then stored in NVS).
#define TEMP_INTERVAL_DEFAULT_S 60
#define TEMP_INTERVAL_MIN_S 10  // must stay above the 750 ms conversion time
#define TEMP_INTERVAL_MAX_S 3600
#define TEMP_INTERVAL_STEP_S 1  // writes are rounded to whole seconds

// Reporting deadband: a reading is only published when it differs from the
// last published one by more than this, in °C. Same precedence as the
// interval - code default, then NVS, then whatever Zigbee writes.
// The comparison is strict, so 0 publishes every reading that differs at all
// from the last published one. The DS18B20's own 12-bit step is 0.0625 °C, so
// any delta below that behaves the same as 0.
#define TEMP_DELTA_DEFAULT_C 0.5f
#define TEMP_DELTA_MIN_C 0.0f
#define TEMP_DELTA_MAX_C 20.0f
#define TEMP_DELTA_STEP_C 0.1f  // writes are rounded to this step

// How often to re-scan the bus while at least one slot has no sensor.
#define ONEWIRE_RESCAN_INTERVAL_MS 60000

/* ------------------------------------------------------------------
 * Pushbutton
 * ------------------------------------------------------------------ */
// Hold this long to wipe all stored configuration and re-pair.
#define FACTORY_RESET_HOLD_MS 5000

// Hold this long before the LED starts showing that a reset is armed.
#define FACTORY_RESET_HINT_MS 500

#define BUTTON_DEBOUNCE_MS 50

/* ------------------------------------------------------------------
 * Zigbee
 * ------------------------------------------------------------------ */
// Temperature sensors occupy EP_TEMP_BASE .. EP_TEMP_BASE+MAX-1.
#define EP_TEMP_BASE 10
// One analog output endpoint per writable setting. An Analog Output cluster
// carries a single value, so each setting needs its own endpoint.
#define EP_CONFIG_INTERVAL 13
#define EP_CONFIG_DELTA 14

#define ZB_MANUFACTURER "M5Stack"
#define ZB_MODEL_INTERVAL "NanoH2-Interval"
#define ZB_MODEL_DELTA "NanoH2-Delta"

/* ------------------------------------------------------------------
 * NVS
 * ------------------------------------------------------------------ */
// Preferences namespace holding our own state. The Zigbee network
// credentials themselves are kept by the Zigbee stack in its own NVS
// namespace and are erased by Zigbee.factoryReset().
#define NVS_NAMESPACE "nanoh2"
#define NVS_KEY_COMMISSIONED "joined"
#define NVS_KEY_INTERVAL "interval"
#define NVS_KEY_DELTA "delta"
#define NVS_KEY_ROM_PREFIX "rom"  // rom0, rom1, rom2
