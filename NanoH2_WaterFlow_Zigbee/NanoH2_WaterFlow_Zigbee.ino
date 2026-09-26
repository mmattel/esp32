/**
 * M5Stack NanoH2 (ESP32-H2, SKU C149) - Hall-effect water flow sensor over Zigbee.
 *
 * Measures water flow with a Hall-effect pulse-output sensor (e.g. YF-S201)
 * powered at 5V and connected to Grove G1 (white) through a logic-level
 * converter.  The sensor's square-wave output is counted by an interrupt and
 * converted to flow rates and a running total.
 *
 * Zigbee endpoints
 * ----------------
 * Writable settings (Analog Output, saved to NVS):
 *   EP 10 - impulses per litre: sensor calibration constant
 *   EP 11 - writeback time (s): inactivity after which the total is saved
 *   EP 12 - total start value (L): writing this sets the running total and
 *           saves it to NVS immediately; use 0 to reset, or carry over an
 *           existing meter reading
 *
 * Read-only (Analog Input):
 *   EP 13 - parent link LQI
 *   EP 14 - parent link RSSI (dBm)
 *   EP 15 - console mirror (line count + text attribute 0xF000)
 *   EP 16 - firmware version (number + text attribute 0xF000)
 *   EP 20 - flow rate, L/min
 *   EP 21 - flow rate, L/s
 *   EP 22 - total consumption, L   (saved to NVS on inactivity)
 *   EP 23 - total consumption, m³
 *   EP 24 - total since last start-value write, L (RAM only, resets on reboot)
 *
 * Arduino IDE settings:
 *   Board            ESP32H2 Dev Module
 *   Zigbee mode      Zigbee ED (end device)
 *   Partition Scheme Zigbee 4MB with spiffs
 *   USB CDC On Boot  Enabled
 *
 * See README.md for wiring.
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
#include "flow_sensor.h"
#include "zb_link.h"
#include "zb_link_endpoint.h"
#include "zb_mirror.h"
#include "zb_setting.h"
#include "zb_version.h"

/* ----------------------------- compile-time checks --------------------- */

static_assert(FW_VERSION_MINOR < 100 && FW_VERSION_PATCH < 100,
              "FW_VERSION_NUMBER gives the minor and the patch two digits each");

static_assert(ZB_CHANNEL == 0 || (ZB_CHANNEL >= 11 && ZB_CHANNEL <= 26),
              "ZB_CHANNEL has to be 0 for all channels, or a channel from 11 to 26");

static_assert(LINK_RETRY_MS <= LINK_INTERVAL_S * 1000L,
              "the link retry has to be shorter than the link interval");

static_assert(BUTTON_STUCK_MS > FACTORY_RESET_HOLD_MS,
              "the stuck threshold has to be above the reset hold");
static_assert(FACTORY_RESET_HOLD_MS > FACTORY_RESET_HINT_MS,
              "the reset hold has to be above the hint");

static_assert(EP_CONFIG_IMPULSES_PER_L != EP_CONFIG_WRITEBACK_S
                && EP_CONFIG_IMPULSES_PER_L != EP_CONFIG_TOTAL_START
                && EP_CONFIG_IMPULSES_PER_L != EP_LINK_LQI
                && EP_CONFIG_IMPULSES_PER_L != EP_LINK_RSSI
                && EP_CONFIG_IMPULSES_PER_L != EP_MIRROR
                && EP_CONFIG_IMPULSES_PER_L != EP_VERSION,
              "impulses-per-litre endpoint number is used twice");
static_assert(EP_CONFIG_WRITEBACK_S != EP_CONFIG_TOTAL_START
                && EP_CONFIG_WRITEBACK_S != EP_LINK_LQI
                && EP_CONFIG_WRITEBACK_S != EP_LINK_RSSI
                && EP_CONFIG_WRITEBACK_S != EP_MIRROR
                && EP_CONFIG_WRITEBACK_S != EP_VERSION,
              "writeback-time endpoint number is used twice");
static_assert(EP_CONFIG_TOTAL_START != EP_LINK_LQI
                && EP_CONFIG_TOTAL_START != EP_LINK_RSSI
                && EP_CONFIG_TOTAL_START != EP_MIRROR
                && EP_CONFIG_TOTAL_START != EP_VERSION,
              "total-start endpoint number is used twice");
static_assert(EP_LINK_LQI != EP_LINK_RSSI && EP_LINK_LQI != EP_MIRROR && EP_LINK_LQI != EP_VERSION
                && EP_LINK_RSSI != EP_MIRROR && EP_LINK_RSSI != EP_VERSION && EP_MIRROR != EP_VERSION,
              "a link, mirror or version endpoint number is used twice");
static_assert(EP_FLOW_L_PER_MIN > EP_VERSION && EP_FLOW_L_PER_S > EP_VERSION
                && EP_TOTAL_L > EP_VERSION && EP_TOTAL_M3 > EP_VERSION
                && EP_TOTAL_SINCE_RESET > EP_VERSION,
              "flow measurement endpoints must sit above the fixed block");

/* ----------------------------- state ----------------------------------- */

enum LinkState { LINK_UNCOMMISSIONED, LINK_CONNECTED, LINK_LOST };

Preferences prefs;
FlowSensor flowSensor(PIN_FLOW);

// Writable settings, each on its own Analog Output endpoint.
ZbSetting cfgImpulsesPerL(EP_CONFIG_IMPULSES_PER_L, NVS_KEY_IMPULSES_PER_L,
                          "Impulses per litre", FLOW_IMPULSES_PER_L_DEFAULT,
                          FLOW_IMPULSES_PER_L_MIN, FLOW_IMPULSES_PER_L_MAX,
                          FLOW_IMPULSES_PER_L_STEP, ESP_ZB_ZCL_AI_APP_TYPE_OTHER);
ZbSetting cfgWritebackS(EP_CONFIG_WRITEBACK_S, NVS_KEY_WRITEBACK_S,
                        "NVS writeback time (s)", FLOW_WRITEBACK_S_DEFAULT,
                        FLOW_WRITEBACK_S_MIN, FLOW_WRITEBACK_S_MAX,
                        FLOW_WRITEBACK_S_STEP, ESP_ZB_ZCL_AI_APP_TYPE_OTHER);
// Writing this from Z2M sets the running total immediately.
// The stored value shows what it was last set to, not the current total.
ZbSetting cfgTotalStart(EP_CONFIG_TOTAL_START, NVS_KEY_TOTAL_START,
                        "Total start value (L)", FLOW_TOTAL_START_DEFAULT,
                        FLOW_TOTAL_START_MIN, FLOW_TOTAL_START_MAX,
                        FLOW_TOTAL_START_STEP, ESP_ZB_ZCL_AI_APP_TYPE_OTHER);

// Link quality towards the parent.
LinkAnalog zbLqi(EP_LINK_LQI);
LinkAnalog zbRssi(EP_LINK_RSSI);

// Console mirror and firmware version.
ZbMirror zbMirror(EP_MIRROR);
ZbVersion zbVersion(EP_VERSION);

// Five read-only flow measurement endpoints; LinkAnalog is used because it
// provides the setAnalogInputUnits() setter to attach a BACnet unit number
// that Zigbee2MQTT picks up and displays.
LinkAnalog zbFlowLPerMin(EP_FLOW_L_PER_MIN);
LinkAnalog zbFlowLPerS(EP_FLOW_L_PER_S);
LinkAnalog zbTotalL(EP_TOTAL_L);
LinkAnalog zbTotalM3(EP_TOTAL_M3);
LinkAnalog zbTotalSinceReset(EP_TOTAL_SINCE_RESET);

// BACnet engineering unit numbers used by Zigbee2MQTT for display labels.
static constexpr uint16_t BACNET_UNIT_L_PER_MIN = 4;
static constexpr uint16_t BACNET_UNIT_L_PER_S   = 3;
static constexpr uint16_t BACNET_UNIT_LITER      = 117;
static constexpr uint16_t BACNET_UNIT_CUBIC_M    = 96;

// The "other" application type used for all custom-unit analog clusters.
static constexpr uint32_t AI_APP_TYPE_OTHER =
  ESP_ZB_ZCL_AI_SET_APP_TYPE_WITH_ID(ESP_ZB_ZCL_AI_APP_TYPE_OTHER, 0xffff);

// Trampolines: onAnalogOutputChange() takes a bare function pointer.
void onImpulsesPerLWritten(float v) { cfgImpulsesPerL.note(v); }
void onWritebackSWritten(float v)   { cfgWritebackS.note(v); }
void onTotalStartWritten(float v)   { cfgTotalStart.note(v); }

// Running totals: accumulated in RAM, persisted to NVS on inactivity.
float totalL = 0.0f;           // main running total (litre)
float totalSinceResetL = 0.0f; // since last start-value write (RAM only)
bool totalDirty = false;       // true when totalL differs from the NVS copy

// Flow state.
float flowLPerMin = 0.0f;
float flowLPerS   = 0.0f;
uint32_t lastSampleMs = 0;
uint32_t lastActiveSampleMs = 0;  // last sample that had any pulses
bool sampleNow = true;

// What was last published, for the per-value heartbeat tracking.
float publishedFlowLPerMin  = -1.0f;
float publishedFlowLPerS    = -1.0f;
float publishedTotalL       = -1.0f;
float publishedTotalM3      = -1.0f;
float publishedSinceResetL  = -1.0f;
uint32_t lastFlowReportMs   = 0;

LinkState linkState = LINK_UNCOMMISSIONED;
bool commissioned  = false;
bool wasConnected  = false;

bool linkPublished    = false;
int16_t lqiPublished  = 0;
int16_t rssiPublished = 0;
uint32_t lastLinkMs   = 0;
uint32_t lastLinkReportMs   = 0;
bool linkNow            = false;
bool linkWaitLogged     = false;
bool linkAssumedLogged  = false;

uint32_t lastSettingReportMs = 0;

bool resetArmed    = false;
bool resetReady    = false;
bool buttonInhibited = false;

/* ------------------------------ LED ------------------------------------ */

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
  digitalWrite(PIN_RGB_POWER, HIGH);
  delay(10);
  rgbLedWrite(PIN_RGB, COLOR_OFF.r, COLOR_OFF.g, COLOR_OFF.b);
}

bool flashOn(uint32_t cycleMs = LED_FLASH_CYCLE_MS) {
  uint32_t phase = millis() % cycleMs;
  return phase < (cycleMs * LED_FLASH_DUTY_PCT) / 100;
}

void updateLed() {
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

void haltFatal(const char *what) {
  logEvent("FATAL: %s", what);
  while (true) {
    ledWrite(flashOn() ? COLOR_FATAL : COLOR_OFF);
    delay(10);
  }
}

/* ----------------------- stored settings ------------------------------- */

void loadSettings() {
  commissioned = prefs.getBool(NVS_KEY_COMMISSIONED, false);
  cfgImpulsesPerL.load(prefs);
  cfgWritebackS.load(prefs);
  cfgTotalStart.load(prefs);

  // The running total is stored under its own key, not via cfgTotalStart:
  // the setting shows what it was last set to; the key holds the live total.
  float stored = prefs.getFloat(NVS_KEY_TOTAL_L, NAN);
  if (!isnan(stored) && stored >= 0) {
    totalL = stored;
    Serial.printf("Running total: %.3f L (from NVS)" CONSOLE_EOL, totalL);
  } else {
    totalL = cfgTotalStart.value();
    Serial.printf("Running total: %.3f L (from start value, no NVS entry yet)" CONSOLE_EOL, totalL);
  }
  totalSinceResetL = 0.0f;
  totalDirty = false;
}

void saveTotalNvs() {
  prefs.putFloat(NVS_KEY_TOTAL_L, totalL);
  totalDirty = false;
  Serial.printf("Total saved to NVS: %.3f L" CONSOLE_EOL, totalL);
}

/* ----------------------------- Zigbee ---------------------------------- */

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
  if (ZB_LQI_ENDPOINT) {
    zbLqi.setAnalogInputReporting(LINK_REPORT_MIN_INTERVAL_S, LINK_REPORT_HEARTBEAT_S, 0);
  }
  if (ZB_RSSI_ENDPOINT) {
    zbRssi.setAnalogInputReporting(LINK_REPORT_MIN_INTERVAL_S, LINK_REPORT_HEARTBEAT_S, 0);
  }
  if (ZB_MIRROR_ENDPOINT) {
    zbMirror.setAnalogInputReporting(MIRROR_REPORT_MIN_INTERVAL_S, MIRROR_REPORT_HEARTBEAT_S, 0);
  }
  // Flow measurements: heartbeat, but no stack-side change filter (we
  // handle deadbands ourselves and report every change explicitly).
  zbFlowLPerMin.setAnalogInputReporting(FLOW_REPORT_MIN_INTERVAL_S, FLOW_REPORT_HEARTBEAT_S, 0);
  zbFlowLPerS.setAnalogInputReporting(FLOW_REPORT_MIN_INTERVAL_S, FLOW_REPORT_HEARTBEAT_S, 0);
  zbTotalL.setAnalogInputReporting(FLOW_REPORT_MIN_INTERVAL_S, FLOW_REPORT_HEARTBEAT_S, 0);
  zbTotalM3.setAnalogInputReporting(FLOW_REPORT_MIN_INTERVAL_S, FLOW_REPORT_HEARTBEAT_S, 0);
  zbTotalSinceReset.setAnalogInputReporting(FLOW_REPORT_MIN_INTERVAL_S, FLOW_REPORT_HEARTBEAT_S, 0);
}

// Adds one Analog Input endpoint for a flow measurement.
// Shared setup: manufacturer/model, application type, min/max, resolution.
static void addFlowAnalogInput(LinkAnalog &ep, const char *description,
                               float minVal, float maxVal, float resolution,
                               uint16_t bacnetUnit) {
  ep.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
  ep.addAnalogInput();
  ep.setAnalogInputApplication(AI_APP_TYPE_OTHER);
  ep.setAnalogInputDescription(description);
  ep.setAnalogInputResolution(resolution);
  ep.setAnalogInputMinMax(minVal, maxVal);
  ep.setAnalogInputUnits(bacnetUnit);
  ep.setPowerSource(ZB_POWER_SOURCE_MAINS);
  Zigbee.addEndpoint(&ep);
}

void setupEndpoints() {
  cfgImpulsesPerL.addEndpoint(onImpulsesPerLWritten);
  cfgWritebackS.addEndpoint(onWritebackSWritten);
  cfgTotalStart.addEndpoint(onTotalStartWritten);
  Serial.printf("EP %u -> impulses per litre" CONSOLE_EOL
                "EP %u -> NVS writeback time (s)" CONSOLE_EOL
                "EP %u -> total start value (L)" CONSOLE_EOL,
                EP_CONFIG_IMPULSES_PER_L, EP_CONFIG_WRITEBACK_S, EP_CONFIG_TOTAL_START);

  if (ZB_LQI_ENDPOINT) {
    zbLqi.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
    zbLqi.addAnalogInput();
    zbLqi.setAnalogInputApplication(ESP_ZB_ZCL_AI_COUNT_UNITLESS_COUNT);
    zbLqi.setAnalogInputDescription("Parent link LQI");
    zbLqi.setAnalogInputResolution(1);
    zbLqi.setAnalogInputMinMax(0, 255);
    zbLqi.setPowerSource(ZB_POWER_SOURCE_MAINS);
    Zigbee.addEndpoint(&zbLqi);
    Serial.printf("EP %u -> parent link LQI" CONSOLE_EOL, EP_LINK_LQI);
  }

  if (ZB_RSSI_ENDPOINT) {
    zbRssi.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
    zbRssi.addAnalogInput();
    zbRssi.setAnalogInputApplication(AI_APP_TYPE_OTHER);
    zbRssi.setAnalogInputUnits(BACNET_UNIT_DBM);
    zbRssi.setAnalogInputDescription("Parent link RSSI");
    zbRssi.setAnalogInputResolution(1);
    zbRssi.setAnalogInputMinMax(-128, 0);
    zbRssi.setPowerSource(ZB_POWER_SOURCE_MAINS);
    Zigbee.addEndpoint(&zbRssi);
    Serial.printf("EP %u -> parent link RSSI, dBm" CONSOLE_EOL, EP_LINK_RSSI);
  }

  if (ZB_MIRROR_ENDPOINT) {
    zbMirror.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
    zbMirror.addAnalogInput();
    zbMirror.setAnalogInputApplication(ESP_ZB_ZCL_AI_COUNT_UNITLESS_COUNT);
    zbMirror.setAnalogInputDescription("Mirror line count");
    zbMirror.setAnalogInputResolution(1);
    zbMirror.setAnalogInputMinMax(0, 65535);
    if (!zbMirror.addText()) {
      logEvent("EP %u: mirrored line attribute unavailable", EP_MIRROR);
    }
    zbMirror.setPowerSource(ZB_POWER_SOURCE_MAINS);
    Zigbee.addEndpoint(&zbMirror);
    Serial.printf("EP %u -> console mirror" CONSOLE_EOL, EP_MIRROR);
  }

  if (ZB_VERSION_ENDPOINT) {
    zbVersion.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
    if (!zbVersion.addSoftwareBuildId(FW_VERSION)) {
      logEvent("EP %u: firmware version attribute unavailable", EP_VERSION);
    }
    zbVersion.addAnalogInput();
    zbVersion.setAnalogInputApplication(ESP_ZB_ZCL_AI_COUNT_UNITLESS_COUNT);
    zbVersion.setAnalogInputDescription("Firmware version number");
    zbVersion.setAnalogInputResolution(1);
    zbVersion.setAnalogInputMinMax(0, 999999);
    if (!zbVersion.addText(FW_VERSION)) {
      logEvent("EP %u: firmware version text unavailable", EP_VERSION);
    }
    zbVersion.setPowerSource(ZB_POWER_SOURCE_MAINS);
    Zigbee.addEndpoint(&zbVersion);
    Serial.printf("EP %u -> firmware version %s" CONSOLE_EOL, EP_VERSION, FW_VERSION);
  }

  addFlowAnalogInput(zbFlowLPerMin, "Flow rate (L/min)",
                     0.0f, 9999.0f, 0.001f, BACNET_UNIT_L_PER_MIN);
  Serial.printf("EP %u -> flow rate, L/min" CONSOLE_EOL, EP_FLOW_L_PER_MIN);

  addFlowAnalogInput(zbFlowLPerS, "Flow rate (L/s)",
                     0.0f, 999.0f, 0.0001f, BACNET_UNIT_L_PER_S);
  Serial.printf("EP %u -> flow rate, L/s" CONSOLE_EOL, EP_FLOW_L_PER_S);

  addFlowAnalogInput(zbTotalL, "Total consumption (L)",
                     0.0f, 9999999.0f, 0.001f, BACNET_UNIT_LITER);
  Serial.printf("EP %u -> total consumption, L" CONSOLE_EOL, EP_TOTAL_L);

  addFlowAnalogInput(zbTotalM3, "Total consumption (m3)",
                     0.0f, 9999.999f, 0.000001f, BACNET_UNIT_CUBIC_M);
  Serial.printf("EP %u -> total consumption, m3" CONSOLE_EOL, EP_TOTAL_M3);

  addFlowAnalogInput(zbTotalSinceReset, "Total since last reset (L)",
                     0.0f, 9999999.0f, 0.001f, BACNET_UNIT_LITER);
  Serial.printf("EP %u -> total since last reset, L" CONSOLE_EOL, EP_TOTAL_SINCE_RESET);
}

// Pushes all five flow values to the coordinator regardless of whether
// they changed.  Called on a join or when forceAll is set.
void publishFlowValues(bool forceAll = false) {
  if (!Zigbee.connected()) {
    return;
  }
  uint32_t now = millis();
  bool heartbeat = (now - lastFlowReportMs) >= (uint32_t)FLOW_REPORT_HEARTBEAT_S * 1000UL;

  if (!forceAll && !heartbeat
      && flowLPerMin  == publishedFlowLPerMin
      && flowLPerS    == publishedFlowLPerS
      && totalL       == publishedTotalL
      && totalSinceResetL == publishedSinceResetL) {
    return;  // nothing new
  }

  float m3 = totalL / 1000.0f;

  zbFlowLPerMin.setAnalogInput(flowLPerMin);
  zbFlowLPerMin.reportAnalogInput();

  zbFlowLPerS.setAnalogInput(flowLPerS);
  zbFlowLPerS.reportAnalogInput();

  zbTotalL.setAnalogInput(totalL);
  zbTotalL.reportAnalogInput();

  zbTotalM3.setAnalogInput(m3);
  zbTotalM3.reportAnalogInput();

  zbTotalSinceReset.setAnalogInput(totalSinceResetL);
  zbTotalSinceReset.reportAnalogInput();

  publishedFlowLPerMin  = flowLPerMin;
  publishedFlowLPerS    = flowLPerS;
  publishedTotalL       = totalL;
  publishedTotalM3      = m3;
  publishedSinceResetL  = totalSinceResetL;
  lastFlowReportMs      = now;
}

void onZigbeeConnected() {
  zbMirror.forgetPublished();
  logEvent("Zigbee connected");

  if (!commissioned) {
    commissioned = true;
    prefs.putBool(NVS_KEY_COMMISSIONED, true);
    Serial.printf("Commissioning stored in NVS" CONSOLE_EOL);
  }

  applyReporting();
  cfgImpulsesPerL.publish();
  cfgWritebackS.publish();
  cfgTotalStart.publish();
  lastSettingReportMs = millis();

  if (ZB_VERSION_ENDPOINT) {
    zbVersion.publish(FW_VERSION_NUMBER);
  }

  // Seed fresh values on every join so the coordinator is not left with
  // stale N/A for the totals.
  publishedFlowLPerMin  = -1.0f;
  publishedFlowLPerS    = -1.0f;
  publishedTotalL       = -1.0f;
  publishedTotalM3      = -1.0f;
  publishedSinceResetL  = -1.0f;
  publishFlowValues(true);

  linkPublished = false;
  linkNow       = true;
  linkWaitLogged   = false;
  linkAssumedLogged = false;
}

/* ----------------------------- joining --------------------------------- */

uint32_t joinWaitStartMs = 0;
uint32_t lastJoinHintMs  = 0;
uint32_t lastJoinScanMs  = 0;
bool joinScanRunning = false;

void resetJoinWait() {
  uint32_t now = millis();
  joinWaitStartMs = now;
  lastJoinHintMs  = now;
  lastJoinScanMs  = now - (uint32_t)JOIN_SCAN_INTERVAL_S * 1000UL;
}

void printNetworksFound(uint16_t found) {
  if (found == 0) {
    Serial.printf("scan: no Zigbee network on any channel" CONSOLE_EOL);
    return;
  }
  zigbee_scan_result_t *nets = Zigbee.getScanResult();
  if (!nets) {
    logEvent("scan: no result to read");
    return;
  }
  Serial.printf("scan: %u network%s in range" CONSOLE_EOL, found, found == 1 ? "" : "s");
  Serial.printf("  PAN ID | CH | joining open | room for an end device" CONSOLE_EOL);
  bool anyOpen = false;
  for (uint16_t i = 0; i < found; i++) {
    Serial.printf("  0x%04X | %2u | %-12s | %s" CONSOLE_EOL,
                  nets[i].short_pan_id, nets[i].logic_channel,
                  nets[i].permit_joining ? "yes" : "no",
                  nets[i].end_device_capacity ? "yes" : "no");
    anyOpen = anyOpen || nets[i].permit_joining;
  }
  if (!anyOpen) {
    Serial.printf("  none of them is open" CONSOLE_EOL);
  }
}

void handleJoining() {
  if (!Zigbee.started()) {
    return;
  }
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
    Zigbee.scanDelete();
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
  Serial.printf("Zigbee: %s, %lus so far" CONSOLE_EOL,
                commissioned ? "still looking for its network" : "still waiting to be commissioned",
                (unsigned long)((now - joinWaitStartMs) / 1000UL));
  if (JOIN_SCAN_INTERVAL_S > 0 && (now - lastJoinScanMs) >= (uint32_t)JOIN_SCAN_INTERVAL_S * 1000UL) {
    lastJoinScanMs  = now;
    joinScanRunning = true;
    Zigbee.scanNetworks(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK, JOIN_SCAN_DURATION);
  }
}

/* ---------------------------- link state ------------------------------- */

void updateLinkState() {
  bool connected = Zigbee.connected();
  if (connected && !wasConnected) {
    onZigbeeConnected();
  } else if (!connected && wasConnected) {
    logEvent("Zigbee link lost");
    resetJoinWait();
  }
  wasConnected = connected;
  linkState = connected ? LINK_CONNECTED
              : commissioned ? LINK_LOST : LINK_UNCOMMISSIONED;
}

bool reportOverdue(uint32_t lastMs, uint32_t heartbeatS) {
  return heartbeatS > 0 && (millis() - lastMs) >= heartbeatS * 1000UL;
}

void handleSettingWrites() {
  cfgImpulsesPerL.applyPending(prefs);
  cfgWritebackS.applyPending(prefs);

  if (cfgTotalStart.applyPending(prefs)) {
    // The user wrote a new total start value: apply it as the running total.
    totalL = cfgTotalStart.value();
    totalSinceResetL = 0.0f;
    saveTotalNvs();
    logEvent("Total reset to %.3f L", totalL);
    publishFlowValues(true);
  }
}

void handleSettingReports() {
  if (!Zigbee.connected() || !reportOverdue(lastSettingReportMs, SETTING_REPORT_HEARTBEAT_S)) {
    return;
  }
  lastSettingReportMs = millis();
  cfgImpulsesPerL.publish();
  cfgWritebackS.publish();
  cfgTotalStart.publish();
  if (ZB_VERSION_ENDPOINT) {
    zbVersion.publish(FW_VERSION_NUMBER);
  }
}

/* ----------------------------- flow ------------------------------------ */

void handleFlow() {
  uint32_t now = millis();
  if (!sampleNow && (now - lastSampleMs) < (uint32_t)FLOW_SAMPLE_INTERVAL_MS) {
    return;
  }
  sampleNow    = false;
  lastSampleMs = now;

  uint32_t pulses = flowSensor.takePulses();
  float ipl = cfgImpulsesPerL.value();

  if (pulses > 0 && ipl > 0) {
    float litresThisSample = (float)pulses / ipl;
    float intervalS = FLOW_SAMPLE_INTERVAL_MS / 1000.0f;
    flowLPerS    = litresThisSample / intervalS;
    flowLPerMin  = flowLPerS * 60.0f;
    totalL          += litresThisSample;
    totalSinceResetL += litresThisSample;
    totalDirty       = true;
    lastActiveSampleMs = now;
  } else {
    flowLPerS   = 0.0f;
    flowLPerMin = 0.0f;

    // Save total to NVS after the sensor has been inactive long enough.
    uint32_t writebackMs = (uint32_t)cfgWritebackS.value() * 1000UL;
    bool inactive = (lastActiveSampleMs != 0)
                    && ((now - lastActiveSampleMs) >= writebackMs);
    if (inactive && totalDirty) {
      saveTotalNvs();
    }
  }

  if (LOG_EVERY_SAMPLE || pulses > 0) {
    Serial.printf("flow: %u pulses  %.3f L/min  %.4f L/s  total %.3f L  since reset %.3f L" CONSOLE_EOL,
                  pulses, flowLPerMin, flowLPerS, totalL, totalSinceResetL);
  }

  publishFlowValues();
}

/* ------------------- link quality and signal strength ------------------ */

uint32_t linkIntervalMs() {
  return (uint32_t)LINK_INTERVAL_S * 1000UL;
}

bool linkMoved(int16_t value, int16_t published, int16_t deadband) {
  int drift = (int)value - (int)published;
  return drift >= deadband || drift <= -deadband;
}

void handleLinkQuality() {
  uint32_t now = millis();
  if (!Zigbee.connected()) {
    return;
  }
  if (!linkNow && (now - lastLinkMs) < linkIntervalMs()) {
    return;
  }
  linkNow   = false;
  lastLinkMs = now;

  LinkQuality link = readParentLink();
  if (!link.valid) {
    if (!linkWaitLogged) {
      if (link.unmeasured) {
        Serial.printf("link: parent 0x%04X found, no measurement yet" CONSOLE_EOL, link.parentAddr);
      } else {
        Serial.printf("link: no parent, neighbour table holds %u entr%s" CONSOLE_EOL,
                      link.entries, link.entries == 1 ? "y" : "ies");
      }
      linkWaitLogged = true;
    }
    lastLinkMs = now - linkIntervalMs() + LINK_RETRY_MS;
    return;
  }
  linkWaitLogged = false;

  if (link.assumed && !linkAssumedLogged) {
    Serial.printf("link: sole neighbour not flagged as parent - reading it as one" CONSOLE_EOL);
    linkAssumedLogged = true;
  }

  bool first     = !linkPublished;
  bool heartbeat = !first && reportOverdue(lastLinkReportMs, LINK_REPORT_HEARTBEAT_S);
  bool due       = first || heartbeat;
  bool lqiMoved  = due || linkMoved(link.lqi,  lqiPublished,  LQI_DELTA);
  bool rssiMoved = due || linkMoved(link.rssi, rssiPublished, RSSI_DELTA);
  bool sendLqi   = ZB_LQI_ENDPOINT  && lqiMoved;
  bool sendRssi  = ZB_RSSI_ENDPOINT && rssiMoved;

  if (lqiMoved || rssiMoved || LOG_EVERY_SAMPLE) {
    Serial.printf("link: parent 0x%04X  LQI %u/255  RSSI %d dBm", link.parentAddr, link.lqi, link.rssi);
    if (!ZB_LQI_ENDPOINT && !ZB_RSSI_ENDPOINT) {
      Serial.printf(CONSOLE_EOL);
    } else if (!sendLqi && !sendRssi) {
      Serial.printf("  within deadband" CONSOLE_EOL);
    } else {
      Serial.printf("  published %s%s%s%s" CONSOLE_EOL CONSOLE_EOL,
                    sendLqi ? "LQI" : "", sendLqi && sendRssi ? " and " : "",
                    sendRssi ? "RSSI" : "",
                    first ? " (first)" : heartbeat ? " (heartbeat)" : "");
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
  if (lqiMoved) {
    lqiPublished = link.lqi;
  }
  if (rssiMoved) {
    rssiPublished = link.rssi;
  }
  if (lqiMoved || rssiMoved) {
    linkPublished    = true;
    lastLinkReportMs = now;
  }
}

/* ---------------------------- pushbutton ------------------------------- */

bool buttonPressed() {
  int level = digitalRead(PIN_BUTTON);
  return BUTTON_ACTIVE_HIGH ? (level == HIGH) : (level == LOW);
}

void probeButtonPin() {
  const int idlePull   = BUTTON_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP;
  const int activePull = BUTTON_ACTIVE_HIGH ? INPUT_PULLUP   : INPUT_PULLDOWN;
  pinMode(PIN_BUTTON, activePull);
  delay(2);
  int towardsPressed = digitalRead(PIN_BUTTON);
  pinMode(PIN_BUTTON, idlePull);
  delay(2);
  int towardsIdle = digitalRead(PIN_BUTTON);
  int mv = -1;
  if (BUTTON_PIN_HAS_ADC) {
    mv = (int)analogReadMilliVolts(PIN_BUTTON);
    pinMode(PIN_BUTTON, idlePull);
    delay(2);
  }
  Serial.printf("  probe: towards pressed %s, towards idle %s" CONSOLE_EOL,
                towardsPressed == HIGH ? "HIGH" : "LOW",
                towardsIdle    == HIGH ? "HIGH" : "LOW");
  if (mv >= 0) {
    Serial.printf("  probe: %d mV on pin, rail %d mV" CONSOLE_EOL, mv, BUTTON_PIN_VDD_MV);
  }
}

void inhibitButton(const char *why) {
  buttonInhibited = true;
  resetArmed  = false;
  resetReady  = false;
  logEvent("Button on pin %d %s - ignoring until idle", PIN_BUTTON, why);
  Serial.printf("  pin reads %s, BUTTON_ACTIVE_HIGH %d counts as pressed" CONSOLE_EOL,
                digitalRead(PIN_BUTTON) == HIGH ? "HIGH" : "LOW", BUTTON_ACTIVE_HIGH);
  probeButtonPin();
}

void checkButtonIdleAtBoot() {
  if (buttonPressed()) {
    inhibitButton("already reads pressed at boot");
  }
}

void factoryReset() {
  logEvent("Factory reset: clearing NVS and re-pairing");
  ledWrite(COLOR_RESET_DONE);
  prefs.clear();
  prefs.end();
  delay(200);
  if (Zigbee.started()) {
    Zigbee.factoryReset();
  } else {
    ESP.restart();
  }
}

// The water-flow sketch has no slots to release, so the button has only
// the factory-reset function.
void handleButton() {
  static bool stable = false;
  static bool candidate = false;
  static uint32_t candidateSinceMs = 0;
  static uint32_t pressedSinceMs   = 0;

  bool now = buttonPressed();
  uint32_t ms = millis();

  if (now != candidate) {
    candidate         = now;
    candidateSinceMs  = ms;
    return;
  }
  if (now != stable && (ms - candidateSinceMs) >= BUTTON_DEBOUNCE_MS) {
    stable = now;
    if (stable) {
      pressedSinceMs = ms;
    } else if (buttonInhibited) {
      buttonInhibited = false;
      logEvent("Button: idle now, back in use");
    } else {
      bool wasReady  = resetReady;
      bool wasArmed  = resetArmed;
      resetArmed = false;
      resetReady = false;
      if (wasReady) {
        factoryReset();
      } else if (wasArmed) {
        logEvent("Button: released before hold was over, no reset");
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
    logEvent("Button: held long enough - release to factory reset");
    Serial.printf(CONSOLE_EOL);
  }
  resetArmed = held >= FACTORY_RESET_HINT_MS;
  resetReady = ready;
}

/* ------------------------- Arduino entry ------------------------------- */

void waitForSerialHost() {
  uint32_t start = millis();
  while (!Serial && (millis() - start) < SERIAL_WAIT_MS) {
    delay(10);
  }
  delay(200);
}

void setup() {
  Serial.begin(115200);
  waitForSerialHost();
  Serial.printf(CONSOLE_EOL "M5Stack NanoH2 - Water Flow over Zigbee v%s" CONSOLE_EOL, FW_VERSION);

  ledBegin();
  pinMode(PIN_BUTTON, BUTTON_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
  delay(1);
  checkButtonIdleAtBoot();

  if (!prefs.begin(NVS_NAMESPACE, false)) {
    logEvent("NVS open failed, running with code defaults");
  }
  loadSettings();

  flowSensor.begin();
  Serial.printf("Flow sensor on pin %d, pulse edge: %s" CONSOLE_EOL,
                PIN_FLOW, FLOW_PULSE_EDGE == FALLING ? "FALLING" : "RISING");

  setupEndpoints();

  if (!zigbeePartitionsPresent()) {
    haltFatal("set Tools -> Partition Scheme to \"Zigbee 4MB with spiffs\" and flash again");
  }

  Zigbee.setRxOnWhenIdle(true);

  if (ZB_CHANNEL != 0) {
    Zigbee.setPrimaryChannelMask(1UL << ZB_CHANNEL);
    Serial.printf("Zigbee: looking on channel %u only" CONSOLE_EOL, ZB_CHANNEL);
  }

  if (!Zigbee.begin(ZIGBEE_END_DEVICE)) {
    logEvent("Zigbee failed to start, rebooting");
    delay(1000);
    ESP.restart();
  }
  Serial.printf("%s" CONSOLE_EOL, commissioned ? "Zigbee started, rejoining known network"
                                               : "Zigbee started, waiting to be commissioned");
  resetJoinWait();
}

void loop() {
  updateLinkState();
  handleJoining();
  handleSettingWrites();
  handleSettingReports();
  zbMirror.handleReports();
  handleFlow();
  handleLinkQuality();
  handleButton();
  updateLed();
  delay(10);
}
