// Central configuration for the NanoH2 DS18B20 Zigbee sensor.
// Everything a user is expected to tune lives in this file.

#pragma once

#include <Arduino.h>

/* ------------------------------------------------------------------
 * Pins - M5Stack NanoH2 (SKU C149)
 *
 * The Grove HY2.0-4P port carries GND (black), 5V (red), G2 (yellow)
 * and G1 (white). The pushbutton sits on G2, the 1-Wire bus on G1;
 * swap the two defines to reverse that.
 * ------------------------------------------------------------------ */
#define PIN_ONEWIRE   1   // Grove white  / G1 - DS18B20 data line
#define PIN_BUTTON    2   // Grove yellow / G2 - pushbutton, feeds 3.3 V when closed
#define PIN_RGB      11   // on-board WS2812 data
#define PIN_RGB_POWER 10  // on-board WS2812 power enable, HIGH = LED powered
#define PIN_LED_BLUE  4   // on-board blue LED, unused here

// The button feeds 3.3 V into G2 when closed, so a closed contact reads HIGH
// and the internal pull-down holds the pin low while the contact is open.
// Set this to 0 for the other common wiring, a button that closes to GND.
#define BUTTON_ACTIVE_HIGH 1

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
static const LedColor COLOR_FATAL = {40, 0, 0};            // red, flashing: cannot run, see serial

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
//
// This is the only place the count is written down; the endpoint numbers
// below and the endpoint objects follow it. Changing it does require a
// factory reset and a re-pair though, see "Changing the sensor count" in
// README.md.
//
// 0 is allowed. The device then has no temperature endpoints and never
// touches PIN_ONEWIRE, leaving the two settings endpoints, the LED and the
// button - a Zigbee-only build. How many sensors are actually plugged in is
// a separate question and never a problem: a slot without its sensor stays
// UNASSIGNED, and an empty bus is a defined state, not an error.
#define MAX_DS18B20_SENSORS 3

// Length of the sketch's per-slot arrays. C++ has no zero-length array, so a
// build with no slots still carries a single unused element; every loop over
// the slots is bounded by MAX_DS18B20_SENSORS, which is what keeps it unused.
#define DS18B20_SLOT_ARRAY_LEN (MAX_DS18B20_SENSORS > 0 ? MAX_DS18B20_SENSORS : 1)

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

// ZCL reporting configuration for the temperature attributes. The deadband
// above is enforced in software and every publish is reported explicitly, so
// the stack must not filter on top of it: the reportable change is fixed at 0.
// The heartbeat is the max_interval, i.e. how often the last published value is
// repeated while readings stay inside the deadband. It keeps the coordinator
// from treating the device as unavailable; 0 would disable periodic reports.
#define TEMP_REPORT_MIN_INTERVAL_S 1
#define TEMP_REPORT_HEARTBEAT_S 3600

/* ------------------------------------------------------------------
 * Pushbutton
 * ------------------------------------------------------------------ */
// Hold this long to wipe all stored configuration and re-pair.
#define FACTORY_RESET_HOLD_MS 5000

// Hold this long before the LED starts showing that a reset is armed.
#define FACTORY_RESET_HINT_MS 500

// The raw level has to hold this long before it is accepted, which swallows
// contact bounce on both press and release.
#define BUTTON_DEBOUNCE_MS 20

/* ------------------------------------------------------------------
 * Zigbee
 * ------------------------------------------------------------------ */
// Temperature sensors occupy EP_TEMP_BASE .. EP_TEMP_BASE+MAX-1.
#define EP_TEMP_BASE 10
// One analog output endpoint per writable setting. An Analog Output cluster
// carries a single value, so each setting needs its own endpoint. They are
// derived from the sensor count so they can never collide with a temperature
// endpoint; with three sensors this is 13 and 14 as before.
#define EP_CONFIG_INTERVAL (EP_TEMP_BASE + MAX_DS18B20_SENSORS)
#define EP_CONFIG_DELTA (EP_TEMP_BASE + MAX_DS18B20_SENSORS + 1)

// One model identifier for the whole device, reported identically by every
// endpoint. A coordinator reads it from the first endpoint that has a Basic
// cluster and treats it as the *product* type - Zigbee2MQTT keys its device
// definition on it - so it must not contain anything instance specific. The
// per-sensor ROM code goes into each endpoint's LocationDescription instead.
// Both strings are limited to 32 characters by the Zigbee library.
#define ZB_MANUFACTURER "M5Stack"
#define ZB_MODEL "NanoH2-DS18B20"

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
#define NVS_KEY_ROM_PREFIX "rom"  // rom0, rom1, ... one key per slot
