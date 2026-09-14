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
 * - A pushbutton on PIN_BUTTON (G2) feeds 3.3 V into the pin when closed; the
 *   pin's internal pull-down holds it low while the contact is open. A short
 *   press takes a reading, holding it wipes all stored configuration.
 * - The on-board RGB LED shows the Zigbee link state.
 *
 * Arduino IDE settings:
 *   Board            ESP32H2 Dev Module   (there is no NanoH2 variant yet)
 *   Zigbee mode      Zigbee ED (end device)
 *   Partition Scheme Zigbee 4MB with spiffs
 *   USB CDC On Boot  Enabled              (for the USB-C serial console)
 *
 * To flash: hold the on-board G9 button, then plug in USB-C.
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
#include "ds18b20_bus.h"
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
static_assert(EP_CONFIG_DELTA <= 240, "Zigbee endpoint numbers have to stay within 1..240");
TempEndpoint *zbTemp[DS18B20_SLOT_ARRAY_LEN] = {nullptr};

// Writable settings, each on its own analog output endpoint.
ZbSetting cfgInterval(EP_CONFIG_INTERVAL, NVS_KEY_INTERVAL, "Reading interval (s)", TEMP_INTERVAL_DEFAULT_S,
                      TEMP_INTERVAL_MIN_S, TEMP_INTERVAL_MAX_S, TEMP_INTERVAL_STEP_S, ESP_ZB_ZCL_AI_TIME_RELATIVE);
ZbSetting cfgDelta(EP_CONFIG_DELTA, NVS_KEY_DELTA, "Reporting delta (C)", TEMP_DELTA_DEFAULT_C, TEMP_DELTA_MIN_C,
                   TEMP_DELTA_MAX_C, TEMP_DELTA_STEP_C, ESP_ZB_ZCL_AI_TEMPERATURE_OTHER);

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

// Forget what the coordinator has: the next valid reading of every slot is
// published whatever the deadband says.
void resetPublished() {
  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    lastPublished[i] = NAN;
  }
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

bool resetArmed = false;  // pushbutton held long enough to show LED feedback

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
  Serial.printf("FATAL: %s\r\n", what);
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

// Maps whatever is on the bus onto the persistent slots: a known ROM keeps its
// slot, an unknown ROM takes the first free one.
void scanSensors() {
  uint64_t found[MAX_DS18B20_SENSORS + 5];
  uint8_t count = owBus.discover(found, sizeof(found) / sizeof(found[0]));
  Serial.printf("1-Wire scan: %u DS18B20 found\r\n", count);

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
        Serial.printf("  %s ignored, all %u slots taken\r\n",
                      DS18B20Bus::romToString(found[f]).c_str(), MAX_DS18B20_SENSORS);
        continue;
      }
      slotRom[slot] = found[f];
      prefs.putULong64(romKey(slot).c_str(), found[f]);
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
    owBus.setResolution12bit(found[f]);
  }

  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    if (slotRom[i] != 0 && !slotPresent[i]) {
      Serial.printf("  slot %u (%s) is configured but missing\r\n", i,
                    DS18B20Bus::romToString(slotRom[i]).c_str());
    }
  }
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
      Serial.printf("Flash partition '%s' is missing\r\n", name);
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
}

// Allocated once and never freed: the endpoints live for the whole run, and
// this has to happen before anything touches zbTemp[].
void createEndpoints() {
  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    zbTemp[i] = new TempEndpoint(EP_TEMP_BASE + i);
  }
}

void setupEndpoints() {
  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    // Same manufacturer and model on every endpoint: this identifies the
    // product. Which sensor an endpoint reads is the sensor id, see above.
    zbTemp[i]->setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
    if (!zbTemp[i]->setSensorId(sensorId(i).c_str())) {
      // Optional attribute: the temperature still works without it, only the
      // "which sensor is this" information is then missing over the air.
      Serial.printf("EP %u: sensor id attribute unavailable\r\n", EP_TEMP_BASE + i);
    }
    zbTemp[i]->setMinMaxValue(-55, 125);  // DS18B20 range
    zbTemp[i]->setTolerance(0.5);
    zbTemp[i]->setDefaultValue(0);
    zbTemp[i]->setPowerSource(ZB_POWER_SOURCE_MAINS);
    Zigbee.addEndpoint(zbTemp[i]);
    Serial.printf("EP %u -> slot %u, sensor %s\r\n", EP_TEMP_BASE + i, i, sensorId(i).c_str());
  }

  cfgInterval.addEndpoint(onIntervalWritten);
  cfgDelta.addEndpoint(onDeltaWritten);
  Serial.printf("EP %u -> reading interval\r\nEP %u -> reporting delta\r\n", EP_CONFIG_INTERVAL, EP_CONFIG_DELTA);
}

void onZigbeeConnected() {
  Serial.println("Zigbee connected");

  if (!commissioned) {
    // The network credentials themselves are written by the Zigbee stack into
    // its own NVS namespace; this flag records that a join ever succeeded, so
    // a later radio loss shows as yellow rather than magenta.
    commissioned = true;
    prefs.putBool(NVS_KEY_COMMISSIONED, true);
    Serial.println("Commissioning stored in NVS");
  }

  applyReporting();  // must be called after Zigbee.begin()
  cfgInterval.publish();
  cfgDelta.publish();

  // Seed the coordinator with fresh values: after a join or a rejoin it has no
  // temperatures at all, and the deadband would otherwise hold them back.
  resetPublished();
  sampleNow = true;
}

void updateLinkState() {
  bool connected = Zigbee.connected();

  if (connected && !wasConnected) {
    onZigbeeConnected();
  } else if (!connected && wasConnected) {
    Serial.println("Zigbee link lost");
  }
  wasConnected = connected;

  if (connected) {
    linkState = LINK_CONNECTED;
  } else {
    linkState = commissioned ? LINK_LOST : LINK_UNCOMMISSIONED;
  }
}

void handleSettingWrites() {
  cfgInterval.applyPending(prefs);  // takes effect on the next sample
  cfgDelta.applyPending(prefs);     // takes effect on the next reading
}

/* --------------------------- temperature -------------------------- */

void readAndPublish() {
  float delta = cfgDelta.value();

  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    if (!slotPresent[i]) {
      continue;
    }
    DS18B20Reading r = owBus.read(slotRom[i]);
    if (!r.valid) {
      Serial.printf("slot %u (%s): read failed\r\n", i,
                    DS18B20Bus::romToString(slotRom[i]).c_str());
      slotPresent[i] = false;  // picked up again by the next rescan
      continue;
    }

    // Deadband: leaving the attribute untouched is what suppresses the report,
    // so nothing can leak out below the threshold. The consequence is that the
    // attribute holds the last published value, which is within delta of the
    // real one by construction.
    bool first = isnan(lastPublished[i]);
    float change = first ? NAN : fabsf(r.celsius - lastPublished[i]);
    bool publish = first || change > delta;

    if (publish) {
      zbTemp[i]->setTemperature(r.celsius);
      if (Zigbee.connected()) {
        // Report explicitly instead of leaving it to the stack's own change
        // detection: that would apply the reportable change from the ZCL
        // reporting configuration on top of our deadband, and the coordinator
        // is free to rewrite it (Zigbee2MQTT sets 1 °C), which would silently
        // override the configured delta. Off the air there is nobody to report
        // to, and a rejoin resets lastPublished anyway.
        zbTemp[i]->reportTemperature();
      }
      lastPublished[i] = r.celsius;
    }

    Serial.printf("slot %u  EP %u  %s  %.2f C  %s\r\n", i, EP_TEMP_BASE + i,
                  DS18B20Bus::romToString(slotRom[i]).c_str(), r.celsius,
                  publish ? (first ? "published (first)" : "published") : "within deadband");
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
        Serial.println("1-Wire: no device responded to CONVERT T");
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

/* ---------------------------- pushbutton -------------------------- */

bool buttonPressed() {
  int level = digitalRead(PIN_BUTTON);
  return BUTTON_ACTIVE_HIGH ? (level == HIGH) : (level == LOW);
}

void factoryReset() {
  Serial.println("Factory reset: clearing NVS and re-pairing");
  ledWrite(COLOR_RESET_DONE);

  prefs.clear();  // slots, interval, delta and the commissioning flag
  prefs.end();
  delay(200);

  if (Zigbee.started()) {
    Zigbee.factoryReset();  // erases the Zigbee NVS namespace and reboots
  } else {
    ESP.restart();
  }
}

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
    } else {
      if (!resetArmed) {
        // Short press: take a reading now instead of waiting out the interval.
        Serial.println("Button: manual reading");
        sampleNow = true;
      }
      resetArmed = false;
    }
  }

  if (stable) {
    uint32_t held = ms - pressedSinceMs;
    if (held >= FACTORY_RESET_HINT_MS) {
      resetArmed = true;
    }
    if (held >= FACTORY_RESET_HOLD_MS) {
      factoryReset();  // does not return
    }
  }
}

/* ------------------------- Arduino entry -------------------------- */

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\r\nM5Stack NanoH2 - DS18B20 over Zigbee");

  ledBegin();
  // Pull the pin to the level the open contact should read, so a disconnected
  // or open button is a defined state rather than a floating one.
  pinMode(PIN_BUTTON, BUTTON_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
  createEndpoints();
  resetPublished();

  if (!prefs.begin(NVS_NAMESPACE, false)) {
    Serial.println("NVS open failed, running with code defaults");
  }
  loadSettings();

  // With no slots the 1-Wire side has nothing to map onto, so the pin is left
  // alone entirely rather than being driven for a scan whose result is unusable.
  if (MAX_DS18B20_SENSORS > 0) {
    owBus.begin();
    scanSensors();
    lastRescanMs = millis();
  } else {
    Serial.println("No sensor slots configured, 1-Wire bus unused");
  }

  setupEndpoints();

  if (!zigbeePartitionsPresent()) {
    haltFatal("set Tools -> Partition Scheme to \"Zigbee 4MB with spiffs\" and flash again");
  }

  // A sleepy end device could not receive the setting writes, so keep the
  // receiver on. The board is USB powered anyway.
  Zigbee.setRxOnWhenIdle(true);

  if (!Zigbee.begin(ZIGBEE_END_DEVICE)) {
    Serial.println("Zigbee failed to start, rebooting");
    delay(1000);
    ESP.restart();
  }
  Serial.println(commissioned ? "Zigbee started, rejoining known network"
                             : "Zigbee started, waiting to be commissioned");
}

void loop() {
  updateLinkState();
  handleSettingWrites();
  handleTemperature();
  handleButton();
  updateLed();
  delay(10);
}
