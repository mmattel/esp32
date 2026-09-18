// Central configuration for the NanoH2 DS18B20 Zigbee sensor.
// Everything a user is expected to tune lives in this file.

#pragma once

#include <Arduino.h>

/* ------------------------------------------------------------------
 * Pins - M5Stack NanoH2 (SKU C149)
 *
 * The Grove HY2.0-4P port carries GND (black), 5V (red), G2 (yellow)
 * and G1 (white). The 1-Wire bus sits on G1 and the pushbutton is the
 * on-board one on G9, beside the USB-C socket, which needs no wiring at
 * all and leaves the Grove port to the bus alone.
 *
 * PIN_BUTTON also takes 2, an external button on Grove yellow - or 1,
 * with PIN_ONEWIRE moved to 2. One define is the whole switch and the
 * code is identical either way, but the choices are not equivalent -
 * see "Which button" below.
 * ------------------------------------------------------------------ */
#define PIN_ONEWIRE   1   // Grove white  / G1 - DS18B20 data line
#define PIN_BUTTON    9   // on-board button / G9 - pulls the pin to GND when closed
#define PIN_RGB      11   // on-board WS2812 data
#define PIN_RGB_POWER 10  // on-board WS2812 power enable, HIGH = LED powered
#define PIN_LED_BLUE  4   // on-board blue LED, unused here

/* ------------------------------------------------------------------
 * Which button
 *
 * PIN_BUTTON 9 - the on-board button, nothing to wire. The default.
 * PIN_BUTTON 2 - an external pushbutton on Grove yellow.
 * PIN_BUTTON 1 - an external pushbutton on Grove white, which needs
 *   PIN_ONEWIRE moved to 2: the bus and the button cannot share a pin.
 *
 * BUTTON_ACTIVE_HIGH 0 is right for all three. An external contact to
 * GND and the on-board button both pull the pin down when closed, and
 * both idle high on the pin's internal pull-up.
 *
 * The code does not care which one it is - buttonPressed(), the
 * debounce, the hold-and-release and both stuck-pin guards are written
 * in terms of these two defines. What the default 9 means, next to an
 * external button on the Grove port:
 *
 * - G9 is the boot strapping pin, which makes it the flashing button
 *   too. Held while the board powers up, it puts the H2 into ROM
 *   download mode and this sketch never runs: dark LED, silent console.
 *   So the factory reset has to be done on a device that is already up -
 *   press, hold, release - and never by holding across a power cycle.
 * - GPIO9 is no ADC channel, so BUTTON_PIN_HAS_ADC below turns itself
 *   off and the stuck-pin report loses its millivolt line. It keeps the
 *   part that pulls the pin both ways, which is the more telling half.
 * - checkButtonIdleAtBoot() has next to nothing left to catch: a pin
 *   genuinely held at boot means the chip is in download mode instead
 *   of running this. It still catches a damaged or shorted switch.
 * - G2 stays free; nothing else here claims it.
 * ------------------------------------------------------------------ */

// The button pulls its pin down to GND when closed, so a closed contact reads LOW
// and the pin is held high while the contact is open - by the internal pull-up
// this switches on, and on a button breakout by its own pull-up resistor too.
// Set this to 1 for the other wiring, a button that feeds 3.3 V into the pin.
// That is a question for an external button only: the on-board one on G9 is
// wired to GND and nothing about it can be changed, so 0 is the only value
// that fits it.
#define BUTTON_ACTIVE_HIGH 0

// For the diagnostic that runs when the button pin looks stuck at the active
// level. The internal pulls are 45 kOhm typical and the input thresholds are
// 0.25/0.75 x VDD (ESP32-H2 datasheet, Table 5-3), so a pin held at the wrong
// end has tens of microamps flowing in - far more than the 50 nA of input
// leakage. That is enough to tell a wiring fault from a floating pin.
#define BUTTON_PULL_KOHM 45
#define BUTTON_PIN_VDD_MV 3300

// GPIO1..GPIO5 are ADC1_CH0..CH4 on the ESP32-H2, so on those the diagnostic can
// report the actual voltage on the pin instead of just the logic level. Which is
// to say an external button on the Grove port gets the voltage, and the on-board
// one on G9 gets the logic level; the test needs no maintenance either way.
#define BUTTON_PIN_HAS_ADC (PIN_BUTTON >= 1 && PIN_BUTTON <= 5)

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
static const LedColor COLOR_RESET_DONE = {40, 40, 40};     // white:   held long enough, release to reset
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
// from the last published one - and 0 is the only setting that does, since the
// step below is coarser than the resolution of a reading.
// The step is a quarter of a degree because a quarter is exact in binary. The
// Analog Output attribute this travels in is a single-precision float, and a
// coordinator snaps its input to the step it reads from the endpoint - so with a
// step of 0.1 a delta of 0.7 arrives, and is shown, as 0.700000010430813. Every
// multiple of 0.25 is representable exactly, in the attribute and in whatever the
// coordinator does its own arithmetic in, so what is set is what is shown.
#define TEMP_DELTA_DEFAULT_C 0.25f
#define TEMP_DELTA_MIN_C 0.0f
#define TEMP_DELTA_MAX_C 20.0f
#define TEMP_DELTA_STEP_C 0.25f  // writes are rounded to this step

// Decimals a reading is rounded to before it is published and printed, so the
// console, the Zigbee attribute and the deadband all work on the same number.
// One digit is the useful precision: the sensor's raw step is 0.0625 °C but its
// accuracy is only ±0.5 °C, so the further digits are noise, and it is still finer
// than the quarter-degree grid the deadband above is set in. 0 rounds to whole
// degrees; raising this only brings back digits the sensor cannot stand behind.
#define TEMP_PUBLISH_DECIMALS 1

// How often to re-scan the bus while at least one slot has no sensor.
#define ONEWIRE_RESCAN_INTERVAL_MS 60000

// ZCL reporting configuration for the temperature attributes. The deadband
// above is enforced in software and every publish is reported explicitly, so
// the stack must not filter on top of it: the reportable change is fixed at 0.
// The heartbeat is the max_interval, i.e. how often the last published value is
// repeated while readings stay inside the deadband. It keeps the coordinator
// from treating the device as unavailable; 0 would disable periodic reports.
//
// The heartbeat is also kept in software: this value is handed to the stack, but
// a coordinator is free to overwrite the reporting configuration it just read -
// Zigbee2MQTT does - so the sketch republishes on its own schedule as well and
// the number here is what actually happens. It is what fills in a value at the
// coordinator that has not moved since the two were bound.
#define TEMP_REPORT_MIN_INTERVAL_S 1
#define TEMP_REPORT_HEARTBEAT_S 3600

// How often the interval and the delta are repeated to the coordinator. They get
// no ZCL reporting configuration at all - the core's analog output cluster takes
// none - so the only thing that reaches a coordinator is what the sketch reports
// itself, and the publish on join is too early: a report goes to whoever is bound,
// and Zigbee2MQTT binds while interviewing. Nothing then changes until something is
// written, which is why a freshly joined device showed no interval and no delta
// until the first write. This closes that. 0 disables it, leaving both readable but
// never sent. They are two attributes of a mains-powered device, so a minute costs
// nothing.
#define SETTING_REPORT_HEARTBEAT_S 60

/* ------------------------------------------------------------------
 * Pushbutton
 *
 * The button has exactly one job: the factory reset, which clears the
 * stored configuration and the Zigbee credentials and is therefore how
 * the device leaves a network. Nothing else is bound to it, and joining
 * a network never involves it - the device does that by itself.
 * ------------------------------------------------------------------ */
// Hold this long and then release to wipe all stored configuration and re-pair.
// The reset fires on the release, not during the hold: a pin that never reads
// idle - a shorted contact, or BUTTON_ACTIVE_HIGH set the wrong way round -
// then cannot wipe the network by itself.
#define FACTORY_RESET_HOLD_MS 5000

// Hold this long before the LED starts showing that a reset is armed.
#define FACTORY_RESET_HINT_MS 500

// A press that lasts this long is not a person holding a button, it is a pin
// stuck at the active level, and the button is ignored until it reads idle
// again. Keep it well above FACTORY_RESET_HOLD_MS so a slow hand is not
// mistaken for a fault.
#define BUTTON_STUCK_MS 30000

// The raw level has to hold this long before it is accepted, which swallows
// contact bounce on both press and release.
#define BUTTON_DEBOUNCE_MS 20

/* ------------------------------------------------------------------
 * Zigbee
 * ------------------------------------------------------------------ */
// Endpoint numbers. Any assignment within 1..240 is legal - the numbers carry no
// meaning of their own - and this one puts what describes the device first and
// the measurements last: the two writable settings, then the two link
// measurements, then the console mirror, then one endpoint per temperature slot
// above them.
//
// The block below is fixed rather than derived from the sensor count, so
// changing MAX_DS18B20_SENSORS no longer moves it. That matters because a
// coordinator names what it finds after the endpoint number, so a number that
// moves renames the setting it belongs to.
//
// One Analog Output or Analog Input cluster carries a single value, so every
// setting and every link measurement needs an endpoint of its own.
#define EP_CONFIG_INTERVAL 10
#define EP_CONFIG_DELTA 11
#define EP_LINK_LQI 12
#define EP_LINK_RSSI 13
#define EP_MIRROR 14

// Temperature sensors occupy EP_TEMP_BASE .. EP_TEMP_BASE+MAX-1. The gap above
// the block leaves room for further settings without moving the sensors.
#define EP_TEMP_BASE 20

// One model identifier for the whole device, reported identically by every
// endpoint. A coordinator reads it from the first endpoint that has a Basic
// cluster and treats it as the *product* type - Zigbee2MQTT keys its device
// definition on it - so it must not contain anything instance specific. The
// per-sensor ROM code goes into each endpoint's LocationDescription instead.
// Both strings are limited to 32 characters by the Zigbee library.
#define ZB_MANUFACTURER "M5Stack"
#define ZB_MODEL "NanoH2-DS18B20"

/* ------------------------------------------------------------------
 * Joining
 *
 * The stack retries network steering once a second until it succeeds,
 * but only logs that at Core Debug Level "Info". These control what the
 * sketch itself prints while it has no network.
 * ------------------------------------------------------------------ */
// How often to report that the device is still looking, in seconds.
// 0 turns the reporting off, scan included.
#define JOIN_HINT_INTERVAL_S 30

// How often to scan for the networks in range while looking, in seconds.
// A scan lists every network it hears with its channel and whether joining
// is open, which is what tells "nothing in range" apart from "in range but
// closed". It shares the radio with the join attempts, so a scan costs
// something; 0 turns scanning off and keeps the hint.
//
// A scan is only ever started along with a hint, so this rounds up to a
// multiple of JOIN_HINT_INTERVAL_S and anything at or below it means every hint
// brings a scan with it. That is the setting for watching a coordinator that
// will not open: the "joining open" column then follows the Permit join window
// closely enough to show it open and close, instead of sampling it every other
// minute and possibly missing it entirely. 120 is the quieter value for a device
// that is simply waiting.
#define JOIN_SCAN_INTERVAL_S 30

// Time spent listening per channel, 1 (fastest) to 4 (most thorough).
// One scan covers all 16 channels, so this is what a scan costs.
#define JOIN_SCAN_DURATION 3

/* ------------------------------------------------------------------
 * Link quality and signal strength
 *
 * Both are measured by this device on the link to its parent, which is
 * the opposite direction to the "linkquality" a coordinator reports.
 * They always go to the serial console; each endpoint additionally puts
 * its value on the air.
 *
 *   LQI  0..255, how well frames from the parent decode. Unitless, and
 *        the number a coordinator would call link quality.
 *   RSSI dBm, how strong they arrive, roughly -30 (next to the parent)
 *        to -95 (about to drop out). This is the signal strength.
 * ------------------------------------------------------------------ */
// 1 creates the read-only Analog Input endpoint for that value, 0 keeps it
// on the console only. Turning either on or off changes the endpoint list,
// which costs a re-pair - see "Changing the sensor count" in README.md, the
// same applies here.
#define ZB_LQI_ENDPOINT 1
#define ZB_RSSI_ENDPOINT 1

// How often the neighbour table is read. One read yields both values.
#define LINK_INTERVAL_S 300

// How far each value has to move from the last published one before the new
// one is published. Both wander on a perfectly good link - the LQI by a few
// counts, the RSSI by a few dB - and a deadband keeps that off the air.
#define LQI_DELTA 10
#define RSSI_DELTA 5

// Retry after this long instead of waiting out the whole interval when the
// stack is connected but has no parent entry yet, which is the state for a
// moment right after a join. Has to stay below LINK_INTERVAL_S.
#define LINK_RETRY_MS 1000

// Repeats the last published values even while they stay inside the deadband,
// like TEMP_REPORT_HEARTBEAT_S does for temperatures, in software as well as in
// the reporting configuration handed to the stack. 0 disables it.
//
// This matters more here than it does for a temperature: a healthy link sits
// still, so both values can stay inside their deadbands for days. Without the
// heartbeat the only report would be the one sent right after the join, which is
// before a coordinator has bound the cluster and therefore goes nowhere - and the
// endpoints would then read as unknown for as long as the link stays good.
#define LINK_REPORT_MIN_INTERVAL_S 1
#define LINK_REPORT_HEARTBEAT_S 3600

/* ------------------------------------------------------------------
 * Console mirror
 *
 * One endpoint that puts the last console line worth an event on the
 * air, as text, so that a coordinator sees what a serial console would
 * have shown: the join, the link going, whatever the button did, an
 * error, and every interval or delta a coordinator changed. It only
 * ever prints - there is nothing that can be written to it.
 *
 * The line sits in a character string attribute of the endpoint's
 * Analog Input cluster, and the value of that cluster counts the lines,
 * so it is the sequence number of the text beside it. That number is
 * what makes a coordinator bind the cluster - which is what lets the
 * text through at all - and it is also the part that is visible without
 * a converter. See "Console mirror" in README.md; Zigbee2MQTT needs a
 * small external converter to show the text itself.
 * ------------------------------------------------------------------ */
// 1 creates the endpoint, 0 keeps every line on the console only. Turning it on
// or off changes the endpoint list, which costs a re-pair - the same as for the
// two link endpoints above.
#define ZB_MIRROR_ENDPOINT 1

// How much of a line is carried. Anything longer is cut off here, which is why
// the mirrored lines are the short ones.
//
// The ceiling is what a single Zigbee frame holds: an APS payload is around 80
// bytes, the report adds a ZCL header and the string its length byte, and a value
// past that would have to be fragmented - which a report is not.
#define MIRROR_TEXT_LEN 64

// The attribute the text lives in. 0xF000 and up is the range reserved for a
// manufacturer's own attributes, which keeps it clear of the standard Analog
// Input attributes (0x001C .. 0x006F) in the same cluster.
#define MIRROR_TEXT_ATTR_ID 0xF000

// Repeats the current line even though no new one came along, like
// LINK_REPORT_HEARTBEAT_S does, and for the same reason: a report goes to
// whoever is bound at that moment, and right after a join that is still nobody -
// so the line announcing the join would otherwise be the one line never seen.
// 0 disables the repeat.
#define MIRROR_REPORT_MIN_INTERVAL_S 1
#define MIRROR_REPORT_HEARTBEAT_S 3600

/* ------------------------------------------------------------------
 * Serial console
 * ------------------------------------------------------------------ */
// How long setup() waits for the USB host to open the port before it starts
// printing, in milliseconds.
//
// The H2 has no UART bridge: the console is the USB Serial/JTAG peripheral, and a
// write issued before the host has the port open runs into the driver's transmit
// timeout. What that discards is the rest of the *buffer*, not the rest of the
// line, so the boot lines did not go missing, they came out cut in half and
// spliced together - "Sensor slots: none configured" and a later "(code default)"
// arrived as "Sensor slots: none confiefault)". Waiting for the host first is what
// keeps the first few lines readable.
//
// The wait ends as soon as the port is open, so a host that is already listening
// costs nothing. It is bounded because a device that runs headless - the normal
// case - has no host to wait for and must boot anyway. 0 skips the wait and prints
// straight away, which is the old behaviour, garbling included.
#define SERIAL_WAIT_MS 2000

// The periodic work - reading the sensors, polling the link, rescanning the bus -
// happens whether or not the result differs from the last one, and saying so
// every time buries the lines that matter. With 0 those three report only when
// something actually changed: a temperature or a link value that passed its
// deadband and was published, a failed read, a scan that found something other
// than last time. Everything that is an event in its own right - joining,
// losing the link, a factory reset, a fault - is printed either way, and is
// exactly what the console mirror above carries.
//
// 1 restores a line per reading and per link poll, including the ones held back
// by a deadband and by how much. That is the view to use when choosing
// TEMP_DELTA_DEFAULT_C, LQI_DELTA or RSSI_DELTA.
#define LOG_EVERY_READING 0

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
