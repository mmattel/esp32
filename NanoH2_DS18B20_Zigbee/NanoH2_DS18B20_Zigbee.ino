/**
 * M5Stack NanoH2 (ESP32-H2, SKU C149) - DS18B20 sensors over Zigbee.
 *
 * - MAX_DS18B20_SENSORS DS18B20 sensors, three by default, share one 1-Wire bus
 *   on PIN_ONEWIRE. Each gets its own Zigbee temperature endpoint: the endpoint
 *   number is the stable slot ID, the endpoint's LocationDescription is the
 *   sensor's own 64-bit ROM code, and the measured value is its temperature.
 *   Slots without a sensor, and a count of 0, are both fine - see config.h.
 * - The bus is read every "interval" seconds; a reading is only published when
 *   it moves more than "delta" degrees from the last published one. Both are
 *   writable from the coordinator and persisted.
 * - The quality (LQI) and the strength (RSSI, in dBm) of the link to the parent
 *   are read on their own interval, logged, and each published on its own analog
 *   input endpoint. This is the device's own view of the link, the opposite
 *   direction to the "linkquality" a coordinator reports.
 * - One further endpoint mirrors the console: the last line worth an event -
 *   the join, the link going, whatever the button did, an error, an interval or
 *   a delta that changed - is published as text, read-only, alongside a count of
 *   those lines. It is how the device says what happened where no serial console
 *   is attached.
 * - While the device has no network it reports the wait and periodically lists
 *   the networks in range with their channel and whether joining is open, which
 *   the stack itself only does at Core Debug Level "Info".
 * - Joining is automatic and needs no button: the stack retries until it gets in.
 * - A pushbutton on PIN_BUTTON pulls the pin down to GND when closed; the pin's
 *   internal pull-up holds it high while the contact is open. That is either the
 *   on-board button on G9 (the default) or an external one on the Grove port
 *   (G2) - one define, no code change, see "Which button" in config.h for what
 *   to expect from each. It does one thing: held for FACTORY_RESET_HOLD_MS and
 *   released, it wipes all stored configuration and the Zigbee credentials, which
 *   is how the device is excluded from a network. A short press does nothing.
 * - The on-board RGB LED shows the Zigbee link state.
 *
 * Arduino IDE settings:
 *   Board            ESP32H2 Dev Module   (there is no NanoH2 variant yet)
 *   Zigbee mode      Zigbee ED (end device)
 *   Partition Scheme Zigbee 4MB with spiffs
 *   USB CDC On Boot  Enabled              (for the USB-C serial console)
 *
 * To flash: hold the on-board G9 button, then plug in USB-C. With the default
 * PIN_BUTTON 9 that is the same button the sketch uses: held during power-up it
 * flashes, held while the sketch runs it factory resets.
 *
 * See README.md for wiring and for how the values appear on the coordinator.
 */

#include <Arduino.h>

#ifndef ZIGBEE_MODE_ED
#error "Zigbee end device mode is not selected in Tools->Zigbee mode"
#endif

#include <Preferences.h>
#include <esp_partition.h>
#include "Zigbee.h"

#include "config.h"
#include "console.h"
#include "ds18b20_bus.h"
#include "zb_link.h"
#include "zb_link_endpoint.h"
#include "zb_mirror.h"
#include "zb_setting.h"
#include "zb_temp_endpoint.h"

/* ----------------------------- state ------------------------------ */

enum LinkState {
  LINK_UNCOMMISSIONED,  // never joined a network
  LINK_CONNECTED,       // joined and on the air
  LINK_LOST             // joined before, no radio contact now
};

Preferences prefs;
DS18B20Bus owBus(PIN_ONEWIRE);

// One endpoint per slot, created unconditionally: the endpoint list is fixed
// at Zigbee.begin() and cannot grow later without re-pairing the device.
// Filled by createEndpoints() rather than being a plain array of objects,
// because TempEndpoint takes its endpoint number in the constructor and there
// is no way to spell "one per slot" for a count that is a macro.
// 0 slots is a valid configuration - no temperature endpoints, no 1-Wire - so
// the only bad count is a negative one, which would otherwise pass silently as
// "no slots" instead of as the typo it is.
static_assert(MAX_DS18B20_SENSORS >= 0, "the sensor count cannot be negative");

// The temperature block is the one that grows with the sensor count, so it is the
// one that can run off the end of the endpoint range.
static_assert(EP_CONFIG_INTERVAL >= 1 && EP_CONFIG_DELTA >= 1 && EP_LINK_LQI >= 1 && EP_LINK_RSSI >= 1
                && EP_MIRROR >= 1 && EP_TEMP_BASE >= 1 && EP_TEMP_BASE + MAX_DS18B20_SENSORS - 1 <= 240,
              "Zigbee endpoint numbers have to stay within 1..240");

// The temperature slots sit above the settings, the link and the mirror, and every
// endpoint number has to be unique: a collision would register two endpoints as one.
static_assert(EP_TEMP_BASE > EP_CONFIG_INTERVAL && EP_TEMP_BASE > EP_CONFIG_DELTA && EP_TEMP_BASE > EP_LINK_LQI
                && EP_TEMP_BASE > EP_LINK_RSSI && EP_TEMP_BASE > EP_MIRROR,
              "the temperature endpoints have to stay above the settings, the link and the mirror endpoints");
static_assert(EP_CONFIG_INTERVAL != EP_CONFIG_DELTA && EP_CONFIG_INTERVAL != EP_LINK_LQI
                && EP_CONFIG_INTERVAL != EP_LINK_RSSI && EP_CONFIG_INTERVAL != EP_MIRROR
                && EP_CONFIG_DELTA != EP_LINK_LQI && EP_CONFIG_DELTA != EP_LINK_RSSI && EP_CONFIG_DELTA != EP_MIRROR
                && EP_LINK_LQI != EP_LINK_RSSI && EP_LINK_LQI != EP_MIRROR && EP_LINK_RSSI != EP_MIRROR,
              "every Zigbee endpoint number has to be used only once");

// The retry shortens the wait for the next link reading, so a value above the
// interval it shortens would mean "retry every loop" instead.
static_assert(LINK_RETRY_MS <= LINK_INTERVAL_S * 1000L, "the link retry has to be shorter than the link interval");

// A press is only judged stuck once it has lasted longer than a deliberate hold,
// otherwise a slow hand would be inhibited instead of resetting.
static_assert(BUTTON_STUCK_MS > FACTORY_RESET_HOLD_MS, "the stuck threshold has to be above the reset hold");
static_assert(FACTORY_RESET_HOLD_MS > FACTORY_RESET_HINT_MS, "the reset hold has to be above the hint");

TempEndpoint *zbTemp[DS18B20_SLOT_ARRAY_LEN] = {nullptr};

// Writable settings, each on its own analog output endpoint.
ZbSetting cfgInterval(EP_CONFIG_INTERVAL, NVS_KEY_INTERVAL, "Reading interval (s)", TEMP_INTERVAL_DEFAULT_S,
                      TEMP_INTERVAL_MIN_S, TEMP_INTERVAL_MAX_S, TEMP_INTERVAL_STEP_S, ESP_ZB_ZCL_AI_TIME_RELATIVE);
ZbSetting cfgDelta(EP_CONFIG_DELTA, NVS_KEY_DELTA, "Reporting delta (C)", TEMP_DELTA_DEFAULT_C, TEMP_DELTA_MIN_C,
                   TEMP_DELTA_MAX_C, TEMP_DELTA_STEP_C, ESP_ZB_ZCL_AI_TEMPERATURE_OTHER);

// The link towards the parent: quality as an LQI, strength as an RSSI in dBm,
// one analog input endpoint each because an Analog Input cluster carries a
// single value. The coordinator only reads these; nothing writes them, so they
// need none of the clamp / persist machinery a ZbSetting has.
LinkAnalog zbLqi(EP_LINK_LQI);
LinkAnalog zbRssi(EP_LINK_RSSI);

// The console mirror. Declared in zb_mirror.h and defined here with the other
// endpoints, because logEvent() reaches it by name from anywhere in the sketch -
// including from the files that know nothing about Zigbee.
ZbMirror zbMirror(EP_MIRROR);

// The ZCL application types have no dBm in the list, so the RSSI endpoint uses
// the "other" group and states its unit in EngineeringUnits instead.
static constexpr uint32_t AI_APP_TYPE_OTHER = ESP_ZB_ZCL_AI_SET_APP_TYPE_WITH_ID(ESP_ZB_ZCL_AI_APP_TYPE_OTHER, 0xffff);

// onAnalogOutputChange() takes a bare function pointer with no user context,
// so each setting gets its own one-line trampoline.
void onIntervalWritten(float value) {
  cfgInterval.note(value);
}
void onDeltaWritten(float value) {
  cfgDelta.note(value);
}

// Slot -> sensor mapping. Persisted, so slot 0 keeps meaning the same physical
// sensor across reboots and across changes in bus enumeration order.
uint64_t slotRom[DS18B20_SLOT_ARRAY_LEN] = {0};
bool slotPresent[DS18B20_SLOT_ARRAY_LEN] = {false};

// Last temperature actually published per slot, which is what the coordinator
// believes. NAN forces the next reading through regardless of the deadband, and
// is what every slot starts out as - see resetPublished().
float lastPublished[DS18B20_SLOT_ARRAY_LEN];

// When each slot last put a value on the air, for the heartbeat that repeats it
// while the readings stay inside the deadband.
uint32_t lastTempReportMs[DS18B20_SLOT_ARRAY_LEN] = {0};

// The last reading that came back valid, per slot, and when it did. For the
// console only, and deliberately separate from lastPublished above: that one is
// what the coordinator has, which the deadband can hold well behind the sensor,
// and its timestamp is the time of a report rather than of a read. Telling "this
// sensor has never worked" from "this sensor worked until four minutes ago" needs
// the read. NAN means no valid reading since boot - which is not the same as an
// empty slot, because a ROM code in NVS only says a sensor was once discovered,
// not that it ever answered with a temperature.
float lastGoodC[DS18B20_SLOT_ARRAY_LEN];
uint32_t lastGoodMs[DS18B20_SLOT_ARRAY_LEN] = {0};

// Whether the 85 C power-on value has already been reported for this slot, so a
// sensor sitting at it costs one line rather than one per interval.
bool porReported[DS18B20_SLOT_ARRAY_LEN] = {false};

// Forget what the coordinator has: the next valid reading of every slot is
// published whatever the deadband says.
void resetPublished() {
  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    lastPublished[i] = NAN;
  }
}

// Called once, from setup(). Not folded into resetPublished(), which also runs on
// every join: what the coordinator knows is forgotten then, but what the hardware
// has done is not - a rejoin says nothing about whether a sensor ever worked.
void initSlotHistory() {
  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    lastGoodC[i] = NAN;
  }
}

// A duration as an age for a console line: seconds below a minute, then minutes,
// then hours and minutes. Printed, never parsed.
String ageText(uint32_t ms) {
  uint32_t seconds = ms / 1000UL;
  if (seconds < 60) {
    return String(seconds) + " s";
  }
  if (seconds < 3600) {
    return String(seconds / 60) + " min";
  }
  return String(seconds / 3600) + " h " + String((seconds % 3600) / 60) + " min";
}

LinkState linkState = LINK_UNCOMMISSIONED;
bool commissioned = false;
bool wasConnected = false;

enum SamplePhase { PHASE_IDLE, PHASE_CONVERTING };
SamplePhase samplePhase = PHASE_IDLE;
uint32_t lastSampleMs = 0;
uint32_t convertStartMs = 0;
uint32_t lastRescanMs = 0;
bool sampleNow = true;  // take one reading as soon as we are up

// What the coordinator has for the link. linkPublished false is the same idea as
// the temperatures' NAN: nothing published yet, so the next reading goes out
// whatever the deadbands say. An RSSI is negative, which rules out a sentinel
// value of its own.
bool linkPublished = false;
int16_t lqiPublished = 0;
int16_t rssiPublished = 0;
uint32_t lastLinkMs = 0;
uint32_t lastLinkReportMs = 0;   // when a link value last went out, for the heartbeat
bool linkNow = false;            // read the link as soon as there is one
bool linkWaitLogged = false;     // "nothing to read" already said once this join
bool linkAssumedLogged = false;  // so has the note about an unflagged parent

// When the two settings last went out, for their heartbeat. See
// handleSettingReports() for why they need one.
uint32_t lastSettingReportMs = 0;

bool resetArmed = false;  // pushbutton held long enough to show LED feedback
bool resetReady = false;  // held the full time: releasing it now resets

// Set while the button reads pressed with no press that could explain it - at
// startup, where nobody can have been holding it since before power-on, or for
// longer than any hand holds a button. Both mean the pin is stuck at the active
// level, and the button is then ignored until it reads idle once. See
// inhibitButton().
bool buttonInhibited = false;

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

bool flashOn() {
  uint32_t phase = millis() % LED_FLASH_CYCLE_MS;
  return phase < ((uint32_t)LED_FLASH_CYCLE_MS * LED_FLASH_DUTY_PCT) / 100;
}

void updateLed() {
  // White means the hold is long enough and the reset happens on release, red
  // that it is not there yet. Both outrank the link state.
  if (resetReady) {
    ledWrite(COLOR_RESET_DONE);
    return;
  }
  if (resetArmed) {
    ledWrite(COLOR_RESET_ARMED);
    return;
  }
  switch (linkState) {
    case LINK_CONNECTED:      ledWrite(COLOR_CONNECTED); break;
    case LINK_UNCOMMISSIONED: ledWrite(flashOn() ? COLOR_UNCOMMISSIONED : COLOR_OFF); break;
    case LINK_LOST:           ledWrite(flashOn() ? COLOR_LINK_LOST : COLOR_OFF); break;
  }
}

// Dead end for a condition no amount of retrying fixes. Rebooting would only
// bury the explanation in a boot loop, so flash red and keep the message on the
// console until the board is reflashed.
void haltFatal(const char *what) {
  logEvent("FATAL: %s", what);
  while (true) {
    ledWrite(flashOn() ? COLOR_FATAL : COLOR_OFF);
    delay(10);
  }
}

/* ------------------------- stored settings ------------------------ */

String romKey(uint8_t slot) {
  return String(NVS_KEY_ROM_PREFIX) + String((int)slot);
}

void loadSettings() {
  commissioned = prefs.getBool(NVS_KEY_COMMISSIONED, false);
  cfgInterval.load(prefs);
  cfgDelta.load(prefs);

  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    slotRom[i] = prefs.getULong64(romKey(i).c_str(), 0);
  }
}

uint32_t intervalMs() {
  return (uint32_t)cfgInterval.value() * 1000UL;
}

/* --------------------------- 1-Wire slots ------------------------- */

// What reportSlotState() last put on the air. 0xFF is not a reachable count -
// MAX_DS18B20_SENSORS caps all three - so the first call always reports, the same
// trick scanSensors() uses for its own first pass.
uint8_t lastOnBus = 0xFF;
uint8_t lastMissing = 0xFF;
uint8_t lastNeverSeen = 0xFF;

// The three slot states as one mirrored line, because the console mirror holds a
// single line and the per-slot detail in scanSensors() would arrive as whichever
// line came last. Sent when the counts change, which includes the first scan after
// boot, and again after a join - see forgetSlotState().
//
//   on the bus  answered the last ROM search
//   missing     a ROM code in NVS, so a sensor was here, and it is not answering
//   never seen  no sensor has ever claimed the slot
//
// A sensor that answers the search but fails every read alternates between the
// first two as the rescan finds it and readAndPublish() drops it again, and is
// reported by name there rather than here.
void reportSlotState() {
  uint8_t onBus = 0, missing = 0, neverSeen = 0;
  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    if (slotPresent[i]) {
      onBus++;
    } else if (slotRom[i] != 0) {
      missing++;
    } else {
      neverSeen++;
    }
  }

  if (onBus == lastOnBus && missing == lastMissing && neverSeen == lastNeverSeen && !LOG_EVERY_READING) {
    return;
  }
  lastOnBus = onBus;
  lastMissing = missing;
  lastNeverSeen = neverSeen;
  logEvent("slots: %u on the bus, %u missing, %u never seen", onBus, missing, neverSeen);
}

// Whatever was last said about the slots went to the previous coordinator, or to
// nobody at all - scanSensors() runs once in setup(), before the radio is up.
// Making the remembered counts impossible again has the next scan repeat it. That
// is one rescan interval away while any slot is empty, and never while all of them
// are reading, where the temperatures themselves are the better answer anyway.
void forgetSlotState() {
  lastOnBus = lastMissing = lastNeverSeen = 0xFF;
}

// Maps whatever is on the bus onto the persistent slots: a known ROM keeps its
// slot, an unknown ROM takes the first free one.
//
// While a slot is empty this runs every ONEWIRE_RESCAN_INTERVAL_MS, and an empty
// bus stays empty for as long as nobody plugs a sensor in, so it reports only
// what differs from the last scan: another number of sensors, or another set of
// slots filled. The header line is printed by whatever has something to say
// below it, which keeps the indented lines under a header of their own.
void scanSensors() {
  static uint8_t lastCount = 0xFF;      // no real count, so the first scan reports
  static uint16_t lastPresent = 0xFFFF;
  uint16_t present = 0;

  uint64_t found[MAX_DS18B20_SENSORS + 5];
  uint8_t count = owBus.discover(found, sizeof(found) / sizeof(found[0]));

  bool headerDone = false;
  auto header = [&]() {
    if (!headerDone) {
      Serial.printf("1-Wire scan: %u DS18B20 found\r\n", count);
      headerDone = true;
    }
  };
  if (count != lastCount || LOG_EVERY_READING) {
    header();
  }

  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    slotPresent[i] = false;
  }

  for (uint8_t f = 0; f < count; f++) {
    int8_t slot = -1;
    for (uint8_t i = 0; i < MAX_DS18B20_SENSORS && slot < 0; i++) {
      if (slotRom[i] == found[f]) {
        slot = i;
      }
    }
    if (slot < 0) {
      for (uint8_t i = 0; i < MAX_DS18B20_SENSORS && slot < 0; i++) {
        if (slotRom[i] == 0) {
          slot = i;
        }
      }
      if (slot < 0) {
        header();
        logEvent("  %s ignored, all %u slots taken", DS18B20Bus::romToString(found[f]).c_str(), MAX_DS18B20_SENSORS);
        continue;
      }
      slotRom[slot] = found[f];
      prefs.putULong64(romKey(slot).c_str(), found[f]);
      header();
      Serial.printf("  %s assigned to slot %d (stored)\r\n",
                    DS18B20Bus::romToString(found[f]).c_str(), slot);
      if (Zigbee.started()) {
        // Tell the coordinator which sensor this endpoint now reads. Before
        // Zigbee.begin() there is nothing to update: setupEndpoints() reads the
        // slot mapping itself.
        zbTemp[slot]->setSensorId(DS18B20Bus::romToString(found[f]).c_str());
      }
    }
    slotPresent[slot] = true;
    present |= (uint16_t)1 << slot;
    owBus.setResolution12bit(found[f]);
  }

  // The missing slots are listed as a set, so they are either all reported or all
  // held back: one of them turning up changes the answer for the others too.
  if (present != lastPresent || LOG_EVERY_READING) {
    for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
      if (slotRom[i] != 0 && !slotPresent[i]) {
        header();
        logEvent("  slot %u (%s) is configured but missing", i, DS18B20Bus::romToString(slotRom[i]).c_str());
      }
    }
    // A slot that has never held a sensor is not a fault, which is why it says
    // so plainly and why it is printed rather than mirrored: three empty slots
    // would otherwise push each other off a mirror that holds one line.
    // reportSlotState() below is what goes on the air for this.
    for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
      if (slotRom[i] == 0) {
        header();
        Serial.printf("  slot %u is unassigned, no sensor has ever claimed it\r\n", i);
      }
    }
  }

  lastCount = count;
  lastPresent = present;
  reportSlotState();
}

bool anySlotMissing() {
  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    if (!slotPresent[i]) {
      return true;
    }
  }
  return false;
}

// False for an empty bus, and for a build with no slots at all. Both are
// legitimate states, they just leave nothing to convert.
bool anySlotPresent() {
  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    if (slotPresent[i]) {
      return true;
    }
  }
  return false;
}

/* ----------------------------- Zigbee ---------------------------- */

// The sensor's own identity, as published in the endpoint's
// LocationDescription: its 16-digit ROM code, or a placeholder while the slot
// has never seen a sensor. The static id is the endpoint number itself.
String sensorId(uint8_t slot) {
  return slotRom[slot] ? DS18B20Bus::romToString(slotRom[slot]) : String("UNASSIGNED");
}

// The Zigbee stack keeps its network credentials and factory data in two flash
// partitions of its own, which only the "Zigbee ..." partition schemes provide.
// Zigbee mode is a separate setting and does not imply them, so a build with the
// default scheme links fine and then aborts inside Zigbee.begin() with
// "ZB_ESP_NVRAM: Failed to find zb_storage partition" - a stack assertion with
// no hint at what to change. Check for the partitions first instead.
bool zigbeePartitionsPresent() {
  static const char *const required[] = {"zb_storage", "zb_fct"};
  bool ok = true;

  for (const char *name : required) {
    if (!esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, name)) {
      logEvent("Flash partition '%s' is missing", name);
      ok = false;
    }
  }
  return ok;
}

void applyReporting() {
  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    // Reportable change 0: every attribute write is worth a report. The
    // deadband lives in readAndPublish() and each publish is reported
    // explicitly, so the stack must not filter a second time on top of that.
    // max_interval repeats the last published value as a heartbeat.
    zbTemp[i]->setReporting(TEMP_REPORT_MIN_INTERVAL_S, TEMP_REPORT_HEARTBEAT_S, 0);
  }
  // Same reasoning as above for the link endpoints: the deadbands are ours, so
  // the stack reports whatever it is given and only adds the heartbeat.
  if (ZB_LQI_ENDPOINT) {
    zbLqi.setAnalogInputReporting(LINK_REPORT_MIN_INTERVAL_S, LINK_REPORT_HEARTBEAT_S, 0);
  }
  if (ZB_RSSI_ENDPOINT) {
    zbRssi.setAnalogInputReporting(LINK_REPORT_MIN_INTERVAL_S, LINK_REPORT_HEARTBEAT_S, 0);
  }
  // Only the sequence number gets a reporting configuration; the text beside it is
  // reported explicitly, which is also why a coordinator rewriting this cannot
  // silence the mirror.
  if (ZB_MIRROR_ENDPOINT) {
    zbMirror.setAnalogInputReporting(MIRROR_REPORT_MIN_INTERVAL_S, MIRROR_REPORT_HEARTBEAT_S, 0);
  }
}

// Allocated once and never freed: the endpoints live for the whole run, and
// this has to happen before anything touches zbTemp[].
void createEndpoints() {
  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    zbTemp[i] = new TempEndpoint(EP_TEMP_BASE + i);
  }
}

// The endpoints are registered in the order they should be read in: settings, link,
// the console mirror, then the temperature slots. The stack reports its endpoints in
// the order they were added here, and a coordinator that lists what it found -
// Zigbee2MQTT among them - follows that order rather than sorting by number.
void setupEndpoints() {
  cfgInterval.addEndpoint(onIntervalWritten);
  cfgDelta.addEndpoint(onDeltaWritten);
  Serial.printf("EP %u -> reading interval\r\nEP %u -> reporting delta\r\n", EP_CONFIG_INTERVAL, EP_CONFIG_DELTA);

  if (ZB_LQI_ENDPOINT) {
    zbLqi.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
    zbLqi.addAnalogInput();
    // Unitless count is what an LQI is: a 0..255 number with no dimension.
    zbLqi.setAnalogInputApplication(ESP_ZB_ZCL_AI_COUNT_UNITLESS_COUNT);
    zbLqi.setAnalogInputDescription("Parent link LQI");
    zbLqi.setAnalogInputResolution(1);
    zbLqi.setAnalogInputMinMax(0, 255);
    zbLqi.setPowerSource(ZB_POWER_SOURCE_MAINS);
    Zigbee.addEndpoint(&zbLqi);
    Serial.printf("EP %u -> parent link LQI\r\n", EP_LINK_LQI);
  }

  if (ZB_RSSI_ENDPOINT) {
    zbRssi.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
    zbRssi.addAnalogInput();
    zbRssi.setAnalogInputApplication(AI_APP_TYPE_OTHER);
    zbRssi.setAnalogInputUnits(BACNET_UNIT_DBM);
    zbRssi.setAnalogInputDescription("Parent link RSSI");
    zbRssi.setAnalogInputResolution(1);
    // An 802.15.4 receiver bottoms out around -100 dBm and cannot see a signal
    // stronger than 0; the int8_t the stack reports spans -128..127.
    zbRssi.setAnalogInputMinMax(-128, 0);
    zbRssi.setPowerSource(ZB_POWER_SOURCE_MAINS);
    Zigbee.addEndpoint(&zbRssi);
    Serial.printf("EP %u -> parent link RSSI, dBm\r\n", EP_LINK_RSSI);
  }

  if (ZB_MIRROR_ENDPOINT) {
    zbMirror.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
    zbMirror.addAnalogInput();
    // The value counts the mirrored lines: a plain number with no dimension, like
    // the LQI, and the part a coordinator can show without understanding the text.
    zbMirror.setAnalogInputApplication(ESP_ZB_ZCL_AI_COUNT_UNITLESS_COUNT);
    // Names the counter, not the endpoint. Zigbee2MQTT takes this string as the expose
    // name for presentValue, so "Console mirror" here would claim that name for the
    // number and leave the text - which is the actual console mirror - to be renamed by
    // hand in every converter. See "Showing the mirrored line" in README.md.
    zbMirror.setAnalogInputDescription("Mirror line count");
    zbMirror.setAnalogInputResolution(1);
    zbMirror.setAnalogInputMinMax(0, 65535);  // the counter is 16 bit and wraps there
    if (!zbMirror.addText()) {
      // Optional attribute, like the sensor id above: without it the endpoint
      // still counts the lines, it just cannot carry them.
      logEvent("EP %u: mirrored line attribute unavailable", EP_MIRROR);
    }
    zbMirror.setPowerSource(ZB_POWER_SOURCE_MAINS);
    Zigbee.addEndpoint(&zbMirror);
    Serial.printf("EP %u -> console mirror\r\n", EP_MIRROR);
  }

  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    // Same manufacturer and model on every endpoint: this identifies the
    // product. Which sensor an endpoint reads is the sensor id, see above.
    zbTemp[i]->setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
    if (!zbTemp[i]->setSensorId(sensorId(i).c_str())) {
      // Optional attribute: the temperature still works without it, only the
      // "which sensor is this" information is then missing over the air.
      logEvent("EP %u: sensor id attribute unavailable", EP_TEMP_BASE + i);
    }
    zbTemp[i]->setMinMaxValue(-55, 125);  // DS18B20 range
    zbTemp[i]->setTolerance(0.5);
    zbTemp[i]->setDefaultValue(0);
    zbTemp[i]->setPowerSource(ZB_POWER_SOURCE_MAINS);
    Zigbee.addEndpoint(zbTemp[i]);
    Serial.printf("EP %u -> slot %u, sensor %s\r\n", EP_TEMP_BASE + i, i, sensorId(i).c_str());
  }
}

void onZigbeeConnected() {
  // Whatever the mirror last published went to the previous network, or to nobody
  // at all, so the join line below is what this one gets first.
  zbMirror.forgetPublished();
  logEvent("Zigbee connected");

  if (!commissioned) {
    // The network credentials themselves are written by the Zigbee stack into
    // its own NVS namespace; this flag records that a join ever succeeded, so
    // a later radio loss shows as yellow rather than magenta.
    commissioned = true;
    prefs.putBool(NVS_KEY_COMMISSIONED, true);
    Serial.println("Commissioning stored in NVS");
  }

  applyReporting();  // must be called after Zigbee.begin()
  // Once now, in case the coordinator is already bound, and then on the heartbeat
  // for the far more likely case that it is not yet - see handleSettingReports().
  cfgInterval.publish();
  cfgDelta.publish();
  lastSettingReportMs = millis();

  // Seed the coordinator with fresh values: after a join or a rejoin it has no
  // temperatures at all, and the deadband would otherwise hold them back.
  resetPublished();
  sampleNow = true;

  // And the slot summary, which a build with nothing on the bus would otherwise
  // never send: the only scan that ran was the one in setup(), before the radio.
  forgetSlotState();

  // Same for the link: a rejoin may well be through a different parent, so the
  // old value says nothing about the new one.
  linkPublished = false;
  linkNow = true;
  linkWaitLogged = false;
  linkAssumedLogged = false;
}

/* ----------------------------- joining ---------------------------- */

// The stack retries network steering once a second for as long as it takes, but
// it only says so at Core Debug Level "Info" - at the default "None" a device
// that cannot find a network looks exactly like one that is not even trying. So
// the sketch reports the wait itself, and every so often scans for what is
// actually on the air, which answers the two questions that matter: is there a
// network in range at all, and is it letting anyone in?
uint32_t joinWaitStartMs = 0;
uint32_t lastJoinHintMs = 0;
uint32_t lastJoinScanMs = 0;
bool joinScanRunning = false;

// Restarts the reporting, so the printed wait is the current one rather than the
// time since boot.
void resetJoinWait() {
  uint32_t now = millis();
  joinWaitStartMs = now;
  lastJoinHintMs = now;
  // Backdated so the first hint brings a scan with it, instead of the wait
  // having to reach JOIN_SCAN_INTERVAL_S first.
  lastJoinScanMs = now - (uint32_t)JOIN_SCAN_INTERVAL_S * 1000UL;
}

void printNetworksFound(uint16_t found) {
  if (found == 0) {
    Serial.println("scan: no Zigbee network on any channel");
    return;
  }

  zigbee_scan_result_t *nets = Zigbee.getScanResult();
  if (nets == nullptr) {
    logEvent("scan: no result to read");
    return;
  }

  bool anyOpen = false;
  Serial.printf("scan: %u network%s in range\r\n", found, found == 1 ? "" : "s");
  Serial.println("  PAN ID | CH | joining open | room for an end device");
  for (uint16_t i = 0; i < found; i++) {
    Serial.printf("  0x%04X | %2u | %-12s | %s\r\n", nets[i].short_pan_id, nets[i].logic_channel, nets[i].permit_joining ? "yes" : "no",
                  nets[i].end_device_capacity ? "yes" : "no");
    anyOpen = anyOpen || nets[i].permit_joining;
  }
  if (!anyOpen) {
    Serial.println("  none of them is open - joining has to be enabled on the coordinator");
  }
}

void handleJoining() {
  if (!Zigbee.started()) {
    return;
  }

  // A scan of ours is in the air: collect it whenever it lands, joined by then
  // or not, because the stack allocated the result and we have to free it.
  if (joinScanRunning) {
    int16_t status = Zigbee.scanComplete();
    if (status == ZB_SCAN_RUNNING) {
      return;
    }
    joinScanRunning = false;
    if (status == ZB_SCAN_FAILED) {
      logEvent("scan: failed");
    } else {
      printNetworksFound((uint16_t)status);
    }
    Zigbee.scanDelete();  // frees the result and re-arms the status
    return;
  }

  if (Zigbee.connected() || JOIN_HINT_INTERVAL_S == 0) {
    return;
  }

  uint32_t now = millis();
  if ((now - lastJoinHintMs) < (uint32_t)JOIN_HINT_INTERVAL_S * 1000UL) {
    return;
  }
  lastJoinHintMs = now;

  Serial.printf("Zigbee: %s, %lus so far\r\n", commissioned ? "still looking for its network" : "still waiting to be commissioned",
                (unsigned long)((now - joinWaitStartMs) / 1000UL));

  if (JOIN_SCAN_INTERVAL_S > 0 && (now - lastJoinScanMs) >= (uint32_t)JOIN_SCAN_INTERVAL_S * 1000UL) {
    lastJoinScanMs = now;
    joinScanRunning = true;
    Zigbee.scanNetworks(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK, JOIN_SCAN_DURATION);
  }
}

/* ---------------------------- link state -------------------------- */

void updateLinkState() {
  bool connected = Zigbee.connected();

  if (connected && !wasConnected) {
    onZigbeeConnected();
  } else if (!connected && wasConnected) {
    logEvent("Zigbee link lost");
    resetJoinWait();  // the wait that starts now is a new one
  }
  wasConnected = connected;

  if (connected) {
    linkState = LINK_CONNECTED;
  } else {
    linkState = commissioned ? LINK_LOST : LINK_UNCOMMISSIONED;
  }
}

// True when the last value put on the air is older than the heartbeat, so it is
// repeated even though nothing moved. A report is only sent to whoever is bound,
// so the one a join produces can predate the binding and be the only one there ever
// was, and the stack's own reporting configuration is no fallback: a coordinator may
// overwrite it (Zigbee2MQTT does), and the settings are given none in the first
// place. Repeating on our own schedule is what closes that, for temperatures, for
// the link and for the two settings alike.
bool reportOverdue(uint32_t lastMs, uint32_t heartbeatS) {
  return heartbeatS > 0 && (millis() - lastMs) >= heartbeatS * 1000UL;
}

void handleSettingWrites() {
  cfgInterval.applyPending(prefs);  // takes effect on the next sample
  cfgDelta.applyPending(prefs);     // takes effect on the next reading
}

// Both settings, repeated on the heartbeat. Nothing else ever reports them: they
// only change when a coordinator writes them, and the coordinator that wrote one
// has the value already - so without this, one that bound after the join publish
// would show no interval and no delta until it wrote one itself.
void handleSettingReports() {
  if (!Zigbee.connected() || !reportOverdue(lastSettingReportMs, SETTING_REPORT_HEARTBEAT_S)) {
    return;
  }
  lastSettingReportMs = millis();
  cfgInterval.publish();
  cfgDelta.publish();
}

/* --------------------------- temperature -------------------------- */

// A reading rounded to TEMP_PUBLISH_DECIMALS. Applied once, right after the read,
// so the deadband, the attribute, the report and the console line are all the same
// number - rounding only on the way to the console would show a value the
// coordinator never got.
float roundReading(float celsius) {
  static const float grid = powf(10.0f, (float)TEMP_PUBLISH_DECIMALS);
  return roundf(celsius * grid) / grid;
}

void readAndPublish() {
  float delta = cfgDelta.value();

  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    if (!slotPresent[i]) {
      continue;
    }
    DS18B20Reading r = owBus.read(slotRom[i]);
    if (!r.valid) {
      // Which of the two failures this is decides what to do about it: a sensor
      // that has never read since boot is wired wrong or dead, one that read fine
      // until a moment ago is a contact or a supply that is going. The mirrored
      // line says which; the age of the last good reading is a console detail,
      // because it changes on every attempt and would defeat the mirror's deadband.
      String rom = DS18B20Bus::romToString(slotRom[i]);
      if (isnan(lastGoodC[i])) {
        logEvent("slot %u (%s): read failed, never read since boot", i, rom.c_str());
      } else {
        logEvent("slot %u (%s): read failed, last good %.*f C", i, rom.c_str(), TEMP_PUBLISH_DECIMALS, lastGoodC[i]);
        Serial.printf("  that reading was %s ago\r\n", ageText(millis() - lastGoodMs[i]).c_str());
      }
      slotPresent[i] = false;  // picked up again by the next rescan
      continue;
    }

    float celsius = roundReading(r.celsius);
    lastGoodC[i] = celsius;
    lastGoodMs[i] = millis();

    // 85.00 C is the temperature register's power-on value, so a sensor stuck at it
    // is one whose supply keeps dropping out, or one being read before its first
    // conversion finished. It is also a temperature a sensor can really be at,
    // which is why this says what the value means and still publishes it. Reported
    // once per spell of it, so a sensor sitting there costs one line, not one per
    // interval; the detail stays off the air for the same reason as above.
    if (r.powerOnReset) {
      if (!porReported[i]) {
        porReported[i] = true;
        logEvent("slot %u (%s): 85.00 C is the power-on default", i, DS18B20Bus::romToString(slotRom[i]).c_str());
        Serial.println("  check the supply and the wiring");
      }
    } else {
      porReported[i] = false;
    }

    // Deadband: leaving the attribute untouched is what suppresses the report,
    // so nothing can leak out below the threshold. The consequence is that the
    // attribute holds the last published value, which is within delta of the
    // real one by construction.
    bool first = isnan(lastPublished[i]);
    float change = first ? NAN : fabsf(celsius - lastPublished[i]);
    bool moved = !first && change > delta;
    bool heartbeat = !first && !moved && reportOverdue(lastTempReportMs[i], TEMP_REPORT_HEARTBEAT_S);
    bool publish = first || moved || heartbeat;

    if (publish) {
      zbTemp[i]->setTemperature(celsius);
      if (Zigbee.connected()) {
        // Report explicitly instead of leaving it to the stack's own change
        // detection: that would apply the reportable change from the ZCL
        // reporting configuration on top of our deadband, and the coordinator
        // is free to rewrite it (Zigbee2MQTT sets 1 °C), which would silently
        // override the configured delta. Off the air there is nobody to report
        // to, and a rejoin resets lastPublished anyway.
        zbTemp[i]->reportTemperature();
      }
      lastPublished[i] = celsius;
      lastTempReportMs[i] = millis();
    }

    // A reading that stayed inside the deadband changed nothing, so it is not
    // worth a line - see LOG_EVERY_READING, which is what to raise while
    // choosing the deadband, since it also prints the readings held back.
    if (publish || LOG_EVERY_READING) {
      Serial.printf("slot %u  EP %u  %s  %.*f C  %s\r\n", i, EP_TEMP_BASE + i,
                    DS18B20Bus::romToString(slotRom[i]).c_str(), TEMP_PUBLISH_DECIMALS, celsius,
                    !publish ? "within deadband"
                             : first ? "published (first)" : heartbeat ? "published (heartbeat)" : "published");
    }
  }
}

void handleTemperature() {
  uint32_t now = millis();

  switch (samplePhase) {
    case PHASE_IDLE:
      if (!sampleNow && (now - lastSampleMs) < intervalMs()) {
        return;
      }
      sampleNow = false;
      if (anySlotMissing() && (now - lastRescanMs) >= ONEWIRE_RESCAN_INTERVAL_MS) {
        lastRescanMs = now;
        scanSensors();
      }
      if (!anySlotPresent()) {
        // Nothing on the bus, or no slots configured at all. The rescan above
        // already reports what it finds, so stay quiet and try again next
        // interval rather than logging a failed conversion every time.
        lastSampleMs = now;
        return;
      }
      if (!owBus.startConversionAll()) {
        logEvent("1-Wire: no device responded to CONVERT T");
        lastSampleMs = now;
        return;
      }
      convertStartMs = now;
      samplePhase = PHASE_CONVERTING;
      break;

    case PHASE_CONVERTING:
      if ((now - convertStartMs) < DS18B20Bus::conversionTimeMs()) {
        return;
      }
      readAndPublish();
      lastSampleMs = millis();
      samplePhase = PHASE_IDLE;
      break;
  }
}

/* ------------------- link quality and signal strength -------------- */

// Quality (LQI) and strength (RSSI) of the link to the parent, on their own
// interval: they have nothing to do with the sensors and they move slowly. One
// read of the neighbour table yields both, and each is published through the
// same kind of deadband the temperatures use.
uint32_t linkIntervalMs() {
  return (uint32_t)LINK_INTERVAL_S * 1000UL;
}

// True when a value has moved far enough from what the coordinator has.
bool linkMoved(int16_t value, int16_t published, int16_t deadband) {
  int drift = (int)value - (int)published;
  return drift >= deadband || drift <= -deadband;
}

void handleLinkQuality() {
  uint32_t now = millis();

  if (!Zigbee.connected()) {
    return;  // no parent, nothing to measure
  }
  if (!linkNow && (now - lastLinkMs) < linkIntervalMs()) {
    return;
  }
  linkNow = false;
  lastLinkMs = now;

  LinkQuality link = readParentLink();
  if (!link.valid) {
    // Connected, but the neighbour table has nothing to read. Right after a join
    // that lasts a moment, so come back in a second instead of after a whole
    // interval - and publishing a 0 here would look like a dead link. Said once
    // per join, or the retries would fill the console; the entry count is in it
    // because "empty" and "holds entries, none of them usable" are different
    // faults and only this line tells them apart.
    if (!linkWaitLogged) {
      Serial.printf("link: no parent to read, neighbour table holds %u entr%s\r\n", link.entries,
                    link.entries == 1 ? "y" : "ies");
      linkWaitLogged = true;
    }
    lastLinkMs = now - linkIntervalMs() + LINK_RETRY_MS;
    return;
  }
  linkWaitLogged = false;

  if (link.assumed && !linkAssumedLogged) {
    Serial.println("link: the one neighbour is not flagged as the parent - reading it as one anyway");
    linkAssumedLogged = true;
  }

  // Nothing shown yet means a join or a rejoin, quite possibly through a
  // different parent, so both values count as having moved. So does a heartbeat
  // that has come round: a link that stays good never leaves its deadbands, and
  // the report a join produces goes out before a coordinator has bound anything.
  bool first = !linkPublished;
  bool heartbeat = !first && reportOverdue(lastLinkReportMs, LINK_REPORT_HEARTBEAT_S);
  bool due = first || heartbeat;
  bool lqiMoved = due || linkMoved(link.lqi, lqiPublished, LQI_DELTA);
  bool rssiMoved = due || linkMoved(link.rssi, rssiPublished, RSSI_DELTA);
  bool sendLqi = ZB_LQI_ENDPOINT && lqiMoved;
  bool sendRssi = ZB_RSSI_ENDPOINT && rssiMoved;

  // A poll that found both values inside their deadbands changed nothing, on the
  // air or on the console, so it says nothing either.
  if (lqiMoved || rssiMoved || LOG_EVERY_READING) {
    Serial.printf("link: parent 0x%04X  LQI %u/255  RSSI %d dBm", link.parentAddr, link.lqi, link.rssi);
    if (!ZB_LQI_ENDPOINT && !ZB_RSSI_ENDPOINT) {
      Serial.println();  // console only, there is nothing to publish to
    } else if (!sendLqi && !sendRssi) {
      Serial.println("  within deadband");
    } else {
      // Blank line after a line that went on the air, so what was published
      // stands apart from the polls around it.
      Serial.printf("  published %s%s%s%s\r\n\r\n", sendLqi ? "LQI" : "", sendLqi && sendRssi ? " and " : "",
                    sendRssi ? "RSSI" : "", first ? " (first)" : heartbeat ? " (heartbeat)" : "");
    }
  }

  if (sendLqi) {
    zbLqi.setAnalogInput(link.lqi);
    zbLqi.reportAnalogInput();
  }
  if (sendRssi) {
    zbRssi.setAnalogInput(link.rssi);
    zbRssi.reportAnalogInput();
  }

  // The deadbands are measured against the last value that was shown - published,
  // or printed where there is no endpoint to publish to - so they move together
  // with the output rather than with the endpoint configuration.
  if (lqiMoved) {
    lqiPublished = link.lqi;
  }
  if (rssiMoved) {
    rssiPublished = link.rssi;
  }
  if (lqiMoved || rssiMoved) {
    linkPublished = true;
    lastLinkReportMs = now;
  }
}

/* ---------------------------- pushbutton -------------------------- */

bool buttonPressed() {
  int level = digitalRead(PIN_BUTTON);
  return BUTTON_ACTIVE_HIGH ? (level == HIGH) : (level == LOW);
}

// Why the pin sits at the active level, which the logic level alone cannot say.
// The pin is read with its normal pull, then with the pull removed, then with the
// opposite pull, and where the pin can do ADC its voltage is measured as well.
// The three causes that look identical from a digitalRead() then separate:
//
//   - a real path to the active rail (a contact that never opens, or a rail wire
//     on the signal pin): the level does not move whatever pull is applied,
//     and the voltage sits at the rail. Holding a pin at the wrong end against a
//     45 kOhm pull needs tens of microamps, which only a conductive path
//     provides - induced noise and leakage are three orders of magnitude short.
//   - an external pull resistor working against the internal one, which is what
//     BUTTON_ACTIVE_HIGH set the wrong way round looks like: a 10 kOhm resistor
//     beats the 45 kOhm internal pull and the level does not move either.
//   - a genuinely floating pin: it follows whichever pull is applied.
//
// Leaves the pin in its configured mode.
void probeButtonPin() {
  const int idlePull = BUTTON_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP;
  const int activePull = BUTTON_ACTIVE_HIGH ? INPUT_PULLUP : INPUT_PULLDOWN;

  pinMode(PIN_BUTTON, activePull);  // pull it the way a press would
  delay(2);
  int towardsPressed = digitalRead(PIN_BUTTON);

  pinMode(PIN_BUTTON, idlePull);  // and back the way an open contact should sit
  delay(2);
  int towardsIdle = digitalRead(PIN_BUTTON);

  // Reading the ADC hands the pad to the analog input, which drops the internal
  // pull, so this is the open-circuit voltage: what is out there on its own.
  int mv = -1;
  if (BUTTON_PIN_HAS_ADC) {
    mv = (int)analogReadMilliVolts(PIN_BUTTON);
    pinMode(PIN_BUTTON, idlePull);
    delay(2);
  }

  Serial.printf("  probe: pulled towards pressed %s, pulled towards idle %s\r\n",
                towardsPressed == HIGH ? "HIGH" : "LOW", towardsIdle == HIGH ? "HIGH" : "LOW");
  if (mv >= 0) {
    Serial.printf("  probe: %d mV on the pin with no internal pull, rail is %d mV\r\n", mv, BUTTON_PIN_VDD_MV);
  }

  if ((towardsIdle == HIGH) != (bool)BUTTON_ACTIVE_HIGH) {
    Serial.printf("  the %d kOhm internal pull moves the pin, so nothing conductive is holding it:\r\n",
                  BUTTON_PULL_KOHM);
    Serial.println("  the active reading was a transient or pick-up on a long run. A 10 kOhm");
    Serial.println("  resistor at the button end holds the idle level far better than the internal one");
    return;
  }

  // V_IH is 0.75 x VDD and the internal pull is 45 kOhm, so ~55 uA has to flow in
  // to hold the pin at the active end. Input leakage is 50 nA - three orders of
  // magnitude short - which leaves a conductive path as the only explanation.
  Serial.printf("  the %d kOhm internal pull cannot move the pin, so tens of microamps are flowing\r\n",
                BUTTON_PULL_KOHM);
  Serial.println("  in: a conductive path is holding it, not noise and not leakage. With the button");
  Serial.println("  open the pin should sit at the idle rail - measure it there. A 4-pin tactile");
  Serial.println("  switch shorts the two legs on the same side, which leaves the contact closed");
  Serial.println("  for good, and a rail wire in the signal position does the same thing");
  Serial.printf("  an external pull resistor also does it - ~10 kOhm beats the %d kOhm internal one.\r\n",
                BUTTON_PULL_KOHM);
  Serial.println("  If the open button measures a few hundred mV off the rail rather than on it, that");
  Serial.printf("  is what it is, and BUTTON_ACTIVE_HIGH %d is the wrong way round for this wiring\r\n",
                BUTTON_ACTIVE_HIGH);
}

// The pin reads pressed with nothing that could be pressing it. Print the level,
// the polarity it is judged against and a probe of what is holding it, then ignore
// the button until it reads idle once so that a fault cannot act as a press.
void inhibitButton(const char *why) {
  buttonInhibited = true;
  resetArmed = false;
  resetReady = false;
  // Only the first line is mirrored: it is the fault itself, and the probe below
  // it is wiring diagnostics that belong to whoever has the console open.
  logEvent("Button on pin %d %s - ignoring it until it goes idle", PIN_BUTTON, why);
  Serial.printf("  the pin reads %s, and with BUTTON_ACTIVE_HIGH %d that counts as pressed\r\n",
                digitalRead(PIN_BUTTON) == HIGH ? "HIGH" : "LOW", BUTTON_ACTIVE_HIGH);
  probeButtonPin();
  Serial.printf("  also check that PIN_BUTTON and PIN_ONEWIRE match the Grove wiring (see README)\r\n");
}

// Nobody can have been holding the button since before power-on, so a pin that
// already reads pressed here is at the active level for some other reason.
void checkButtonIdleAtBoot() {
  if (buttonPressed()) {
    inhibitButton("already reads pressed at boot");
  }
}

// Reached only from a released hold, never from a level that merely reads
// pressed - see handleButton().
void factoryReset() {
  // Mirrored before anything is wiped, so the coordinator hears why the device is
  // about to leave - the report goes out while the network is still there.
  logEvent("Factory reset: clearing NVS and re-pairing");
  ledWrite(COLOR_RESET_DONE);  // white already, and stays so through the wipe

  prefs.clear();  // slots, interval, delta and the commissioning flag
  prefs.end();
  delay(200);

  if (Zigbee.started()) {
    Zigbee.factoryReset();  // erases the Zigbee NVS namespace and reboots
  } else {
    ESP.restart();
  }
}

// Hold for FACTORY_RESET_HOLD_MS and release to factory reset. That is the whole
// button: nothing is bound to a short press, so a press that was never meant -
// or never happened - changes nothing.
//
// The reset fires on the *release*, which is what makes a faulty pin harmless. An
// inverted or shorted pin reads pressed and never lets go, so it never produces a
// release; firing during the hold instead meant such a pin wiped the network
// FACTORY_RESET_HOLD_MS into every boot and rebooted into the same state, which
// left no run long enough to join and looked like joining needed the button.
void handleButton() {
  static bool stable = false;
  static bool candidate = false;
  static uint32_t candidateSinceMs = 0;
  static uint32_t pressedSinceMs = 0;

  bool now = buttonPressed();
  uint32_t ms = millis();

  if (now != candidate) {
    candidate = now;
    candidateSinceMs = ms;
    return;
  }
  if (now != stable && (ms - candidateSinceMs) >= BUTTON_DEBOUNCE_MS) {
    stable = now;
    if (stable) {
      pressedSinceMs = ms;
    } else if (buttonInhibited) {
      // The idle level we were waiting for: the button is real after all.
      buttonInhibited = false;
      logEvent("Button: idle now, back in use");
    } else {
      bool wasArmed = resetArmed;
      bool wasReady = resetReady;
      resetArmed = false;
      resetReady = false;
      if (wasReady) {
        factoryReset();  // does not return
      } else if (wasArmed) {
        logEvent("Button: released before the hold was over, no reset");
      }
    }
  }

  if (!stable || buttonInhibited) {
    return;
  }

  uint32_t held = ms - pressedSinceMs;
  if (held >= BUTTON_STUCK_MS) {
    inhibitButton("has read pressed for longer than any hold could last");
    return;
  }
  bool ready = held >= FACTORY_RESET_HOLD_MS;
  if (ready && !resetReady) {
    // Blank line after it: what follows a release is either the reset log or
    // nothing at all, and the gap keeps the prompt apart from both. It is printed
    // separately, since a mirrored line carries no blank line of its own.
    logEvent("Button: held long enough - release to factory reset");
    Serial.println();
  }
  resetArmed = held >= FACTORY_RESET_HINT_MS;
  resetReady = ready;
}

/* ------------------------- Arduino entry -------------------------- */

// Waits for the USB host to open the console, so the first lines are not written
// into a port nobody has opened yet - see SERIAL_WAIT_MS for what that did to them.
// Bounded, because a device with no host attached still has to boot.
//
// Whether "Serial" can report the host at all depends on the console driver, and a
// driver that never says connected is harmless here: the wait then simply runs to
// SERIAL_WAIT_MS, which is the fixed delay this replaced, only longer.
void waitForSerialHost() {
  uint32_t start = millis();
  while (!Serial && (millis() - start) < SERIAL_WAIT_MS) {
    delay(10);
  }
  // The port being open is not quite the same as the far end being ready to read
  // from it, and this once is not on any critical path.
  delay(200);
}

void setup() {
  Serial.begin(115200);
  waitForSerialHost();
  Serial.println("\r\nM5Stack NanoH2 - DS18B20 over Zigbee");
  // The slot count is a compile-time choice and the endpoints, the NVS keys and
  // the whole 1-Wire side follow it, so it is stated before anything that depends
  // on it. Zero prints as "no" rather than as a 0, which reads as the deliberate
  // configuration it is instead of a count that failed to print.
  if (MAX_DS18B20_SENSORS > 0) {
    Serial.printf("Sensor slots: %u\r\n", (unsigned)MAX_DS18B20_SENSORS);
  } else {
    Serial.println("Sensor slots: none configured");
  }

  ledBegin();
  // Pull the pin to the level the open contact should read, so a disconnected
  // or open button is a defined state rather than a floating one.
  pinMode(PIN_BUTTON, BUTTON_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
  delay(1);  // let the internal pull win over the pin's boot state
  checkButtonIdleAtBoot();
  createEndpoints();
  resetPublished();
  initSlotHistory();  // before the first scan, which is the first thing that reads

  if (!prefs.begin(NVS_NAMESPACE, false)) {
    logEvent("NVS open failed, running with code defaults");
  }
  loadSettings();

  // With no slots the 1-Wire side has nothing to map onto, so the pin is left
  // alone entirely rather than being driven for a scan whose result is unusable.
  if (MAX_DS18B20_SENSORS > 0) {
    owBus.begin();
    scanSensors();
    lastRescanMs = millis();
  } else {
    Serial.println("1-Wire bus unused, no slots to map a sensor onto");
  }

  setupEndpoints();

  if (!zigbeePartitionsPresent()) {
    haltFatal("set Tools -> Partition Scheme to \"Zigbee 4MB with spiffs\" and flash again");
  }

  // A sleepy end device could not receive the setting writes, so keep the
  // receiver on. The board is USB powered anyway.
  Zigbee.setRxOnWhenIdle(true);

  if (!Zigbee.begin(ZIGBEE_END_DEVICE)) {
    logEvent("Zigbee failed to start, rebooting");
    delay(1000);
    ESP.restart();
  }
  Serial.println(commissioned ? "Zigbee started, rejoining known network"
                             : "Zigbee started, waiting to be commissioned");
  resetJoinWait();
}

void loop() {
  updateLinkState();
  handleJoining();
  handleSettingWrites();
  handleSettingReports();
  zbMirror.handleReports();
  handleTemperature();
  handleLinkQuality();
  handleButton();
  updateLed();
  delay(10);
}
