// Central configuration for the NanoH2 Hall-sensor water-flow Zigbee device.
// Everything a user is expected to tune lives in this file.

#pragma once

#include <Arduino.h>

/* ------------------------------------------------------------------
 * Firmware version
 *
 * Semantic versioning - bump in the same commit as the change:
 *   patch: fix that changes nothing the coordinator sees
 *   minor: new feature, endpoint list and expose names unchanged
 *   major: added/removed endpoint, renamed expose, NVS incompatible
 * ------------------------------------------------------------------ */
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0

#define FW_VERSION_STRINGIFY_(x) #x
#define FW_VERSION_STRINGIFY(x) FW_VERSION_STRINGIFY_(x)
#define FW_VERSION                                                                        \
  FW_VERSION_STRINGIFY(FW_VERSION_MAJOR) "." FW_VERSION_STRINGIFY(FW_VERSION_MINOR) "."   \
    FW_VERSION_STRINGIFY(FW_VERSION_PATCH)

// 1.0.0 -> 10000; two digits each for minor and patch.
#define FW_VERSION_NUMBER (FW_VERSION_MAJOR * 10000 + FW_VERSION_MINOR * 100 + FW_VERSION_PATCH)

/* ------------------------------------------------------------------
 * Pins - M5Stack NanoH2 (SKU C149)
 *
 * The Grove HY2.0-4P port carries GND (black), 5V (red), G2 (yellow)
 * and G1 (white). The Hall sensor signal connects to G1 (white) through
 * a logic-level converter: the sensor is powered from the Grove 5V rail,
 * its open-drain output is pulled up to 5V, and the LLC converts the
 * 5V square wave to 3.3V for the NanoH2.
 *
 * PIN_FLOW and PIN_ONEWIRE are the same physical pin on purpose: the two
 * sketches serve different sensors on the same Grove port, never together.
 * ------------------------------------------------------------------ */
#define PIN_FLOW      1   // Grove white / G1 - Hall sensor signal (via LLC)
#define PIN_BUTTON    9   // on-board button / G9
#define PIN_RGB      11   // on-board WS2812 data
#define PIN_RGB_POWER 10  // on-board WS2812 power enable, HIGH = LED powered
#define PIN_LED_BLUE  4   // on-board blue LED, unused here

// The sensor output is open-drain with a pull-up: it pulses LOW on each
// revolution of the paddle wheel, so we trigger on the falling edge.
#define FLOW_PULSE_EDGE FALLING

/* ------------------------------------------------------------------
 * Which button
 *
 * BUTTON_ACTIVE_HIGH 0: the on-board button on G9 and any external button
 * wired contact-to-GND both pull the pin down when closed.
 * ------------------------------------------------------------------ */
#define BUTTON_ACTIVE_HIGH 0

#define BUTTON_PULL_KOHM 45
#define BUTTON_PIN_VDD_MV 3300
#define BUTTON_PIN_HAS_ADC (PIN_BUTTON >= 1 && PIN_BUTTON <= 5)

/* ------------------------------------------------------------------
 * LED
 * ------------------------------------------------------------------ */
struct LedColor {
  uint8_t r, g, b;
};

static const LedColor COLOR_OFF            = {0,  0,  0};
static const LedColor COLOR_UNCOMMISSIONED = {40, 0, 40};  // magenta
static const LedColor COLOR_CONNECTED      = {0, 40,  0};  // green
static const LedColor COLOR_LINK_LOST      = {40, 30, 0};  // yellow
static const LedColor COLOR_RESET_ARMED    = {40, 0,  0};  // red
static const LedColor COLOR_RESET_DONE     = {40, 40, 40}; // white
static const LedColor COLOR_FATAL          = {40, 0,  0};  // red, flashing

#define LED_FLASH_CYCLE_MS 3000
#define LED_FLASH_DUTY_PCT 50

/* ------------------------------------------------------------------
 * Hall-effect flow sensor
 * ------------------------------------------------------------------ */
// Impulses per litre: the sensor-specific calibration factor.
// A YF-S201 is typically 450 Hz/(L/min) at 1 L/min, but the exact value
// should be verified against a reference volume.
#define FLOW_IMPULSES_PER_L_DEFAULT  450.0f
#define FLOW_IMPULSES_PER_L_MIN      0.0f
#define FLOW_IMPULSES_PER_L_MAX      5000.0f
#define FLOW_IMPULSES_PER_L_STEP     0.001f

// How long the sensor must be inactive (no pulses) before the running
// total is written back to NVS.  Keeps NVS write cycles low while still
// saving promptly after the tap is closed.
#define FLOW_WRITEBACK_S_DEFAULT     5
#define FLOW_WRITEBACK_S_MIN         1
#define FLOW_WRITEBACK_S_MAX         20
#define FLOW_WRITEBACK_S_STEP        1

// Start value for the running total (litres).  Writing this from Z2M
// resets the total counter to the given value and saves it to NVS
// immediately.  Use 0 to reset to zero, or a non-zero value to carry
// over an existing meter reading.
#define FLOW_TOTAL_START_DEFAULT     0.0f
#define FLOW_TOTAL_START_MIN         0.0f
#define FLOW_TOTAL_START_MAX         9999999.0f
#define FLOW_TOTAL_START_STEP        0.001f

// How often flow rate is computed and, when connected, published.
#define FLOW_SAMPLE_INTERVAL_MS      1000

// The four measurement attributes are republished at this interval even
// when the values have not changed, so the coordinator knows the device
// is alive and the value really is zero.
#define FLOW_REPORT_MIN_INTERVAL_S 1
#define FLOW_REPORT_HEARTBEAT_S    3600

// How often the three settings are repeated to the coordinator. Same role
// as SETTING_REPORT_HEARTBEAT_S in the DS18B20 sketch: a join publish
// precedes the coordinator's binding, so without this the settings would
// be invisible until the coordinator wrote them. 0 disables.
#define SETTING_REPORT_HEARTBEAT_S 60

/* ------------------------------------------------------------------
 * Pushbutton
 * ------------------------------------------------------------------ */
#define FACTORY_RESET_HOLD_MS  5000
#define FACTORY_RESET_HINT_MS   500
#define BUTTON_STUCK_MS        30000
#define BUTTON_DEBOUNCE_MS        20

/* ------------------------------------------------------------------
 * Zigbee endpoints
 *
 * Block 10-16 is fixed: an endpoint number that moves renames the
 * expose on every coordinator that already knows this device.
 * Flow measurements start at 20, matching the DS18B20 sketch's pattern
 * of leaving a gap for future fixed endpoints.
 *
 * Writable settings (Analog Output):
 *   10 - impulses per litre
 *   11 - writeback time (s)
 *   12 - total start value (L)
 *
 * Read-only diagnostics (Analog Input):
 *   13 - parent link LQI
 *   14 - parent link RSSI
 *   15 - console mirror (sequence count + text attribute 0xF000)
 *   16 - firmware version (number + text attribute 0xF000)
 *
 * Flow measurements (Analog Input):
 *   20 - flow rate, L/min
 *   21 - flow rate, L/s
 *   22 - total consumption, L
 *   23 - total consumption, m³
 *   24 - total since last start-value write, L
 * ------------------------------------------------------------------ */
#define EP_CONFIG_IMPULSES_PER_L  10
#define EP_CONFIG_WRITEBACK_S     11
#define EP_CONFIG_TOTAL_START     12
#define EP_LINK_LQI               13
#define EP_LINK_RSSI              14
#define EP_MIRROR                 15
#define EP_VERSION                16

#define EP_FLOW_L_PER_MIN         20
#define EP_FLOW_L_PER_S           21
#define EP_TOTAL_L                22
#define EP_TOTAL_M3               23
#define EP_TOTAL_SINCE_RESET      24

// One model identifier for the whole device. Zigbee2MQTT keys its device
// definition on ZB_MODEL, so it must never contain a version string.
#define ZB_MANUFACTURER "M5Stack"
#define ZB_MODEL "NanoH2-WaterFlow"

// 1 = create the endpoint, 0 = omit it.  Toggling any of these changes
// the endpoint list and requires a factory reset and re-pair.
#define ZB_LQI_ENDPOINT     1
#define ZB_RSSI_ENDPOINT    1
#define ZB_MIRROR_ENDPOINT  1
#define ZB_VERSION_ENDPOINT 1

/* ------------------------------------------------------------------
 * Joining
 * ------------------------------------------------------------------ */
// 0 scans all channels 11-26; set to a specific channel to speed up
// joining when the coordinator's channel is known.
#define ZB_CHANNEL 0

#define JOIN_HINT_INTERVAL_S  30
#define JOIN_SCAN_INTERVAL_S  30
#define JOIN_SCAN_DURATION     3

/* ------------------------------------------------------------------
 * Link quality and signal strength
 * ------------------------------------------------------------------ */
#define LINK_INTERVAL_S          300
#define LQI_DELTA                 10
#define RSSI_DELTA                 5
#define LINK_RETRY_MS           1000
#define LINK_REPORT_MIN_INTERVAL_S 1
#define LINK_REPORT_HEARTBEAT_S 3600

/* ------------------------------------------------------------------
 * Console mirror
 * ------------------------------------------------------------------ */
#define MIRROR_TEXT_LEN             64
#define MIRROR_TEXT_ATTR_ID     0xF000
#define MIRROR_REPORT_MIN_INTERVAL_S 1
#define MIRROR_REPORT_HEARTBEAT_S 3600

/* ------------------------------------------------------------------
 * Firmware version endpoint
 * ------------------------------------------------------------------ */
#define VERSION_TEXT_ATTR_ID    0xF000
#define VERSION_SW_BUILD_ID_ATTR 0x4000

/* ------------------------------------------------------------------
 * Serial console
 * ------------------------------------------------------------------ */
#define SERIAL_WAIT_MS 2000

// Bare LF, not CR+LF - see the DS18B20 config.h for the full explanation.
#define CONSOLE_EOL "\n"

// 1 prints a line for every flow sample even when the value has not moved.
// Useful for tuning the impulses-per-litre calibration.
#define LOG_EVERY_SAMPLE 0

/* ------------------------------------------------------------------
 * NVS
 * ------------------------------------------------------------------ */
#define NVS_NAMESPACE           "nanoh2flow"
#define NVS_KEY_COMMISSIONED    "joined"
#define NVS_KEY_IMPULSES_PER_L  "ipl"
#define NVS_KEY_WRITEBACK_S     "wbs"
#define NVS_KEY_TOTAL_START     "tstart"
#define NVS_KEY_TOTAL_L         "total"
