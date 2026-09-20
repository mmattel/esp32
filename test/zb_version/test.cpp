// Host tests for the firmware version endpoint: the two attributes it creates, what
// they hold, and what goes on the air when it publishes.
//
// Arduino.h and Zigbee.h in this directory are stubs. The attribute creation calls are
// recorded rather than performed, because what matters here is exactly what the sketch
// asks the stack for: which cluster, which attribute, which type, which access, and a
// ZCL character string whose leading length byte agrees with the characters after it.
// That length byte is the part no compiler checks and the board cannot be asked about.

#include "Arduino.h"
#include "../../NanoH2_DS18B20_Zigbee/zb_version.cpp"
#include <cassert>

// What esp_zb_cluster_add_attr() was handed, and whether it should refuse.
static bool clusterMissing = false;
static bool addAttrFails = false;
static bool updateFails = false;
static uint16_t askedForCluster = 0xFFFF;  // the cluster esp_zb_cluster_list_get_cluster() was asked for
static int attrsAdded = 0;
static uint16_t createdCluster = 0, createdAttr = 0;
static uint8_t createdType = 0, createdAccess = 0, createdLenByte = 0;
static char createdValue[VERSION_TEXT_MAX + 2] = "";
static int attrsUpdated = 0;
static uint16_t updatedAttr = 0;
static uint8_t updatedLenByte = 0;
static char updatedValue[VERSION_TEXT_MAX + 2] = "";

// What went on the air, and in which order: the string has to arrive before the number
// it belongs to, so a coordinator acting on the number already has the version.
static int airEvents = 0;
static int textReportedAt = 0, valueReportedAt = 0;
static int textReports = 0, valueReports = 0;
static esp_zb_zcl_report_attr_cmd_t lastReport;
static float wroteValue = NAN;

const char *esp_err_to_name(esp_err_t) {
  return "ESP_ERR (stub)";
}

esp_zb_attribute_list_t *esp_zb_cluster_list_get_cluster(esp_zb_cluster_list_t *, uint16_t cluster_id, uint8_t) {
  askedForCluster = cluster_id;
  // A non-null pointer is all the sketch does with it: it hands it straight back to
  // the two calls below, which are stubs as well.
  static int placeholder = 0;
  return clusterMissing ? nullptr : (esp_zb_attribute_list_t *)&placeholder;
}

esp_err_t esp_zb_cluster_add_attr(esp_zb_attribute_list_t *, uint16_t cluster_id, uint16_t attr_id, uint8_t attr_type,
                                  uint8_t attr_access, void *value_p) {
  if (addAttrFails) {
    return ESP_ERR_INVALID_STATE;
  }
  const char *zcl = (const char *)value_p;
  attrsAdded++;
  createdCluster = cluster_id;
  createdAttr = attr_id;
  createdType = attr_type;
  createdAccess = attr_access;
  // Only as much as the length byte says, which is what a coordinator would be given.
  createdLenByte = (uint8_t)zcl[0];
  memcpy(createdValue, zcl + 1, createdLenByte);
  createdValue[createdLenByte] = '\0';
  return ESP_OK;
}

esp_err_t esp_zb_cluster_update_attr(esp_zb_attribute_list_t *, uint16_t attr_id, void *value_p) {
  if (updateFails) {
    return ESP_ERR_INVALID_STATE;
  }
  const char *zcl = (const char *)value_p;
  attrsUpdated++;
  updatedAttr = attr_id;
  updatedLenByte = (uint8_t)zcl[0];
  memcpy(updatedValue, zcl + 1, updatedLenByte);
  updatedValue[updatedLenByte] = '\0';
  return ESP_OK;
}

bool ZigbeeEP::reportClusterAttribute(esp_zb_zcl_report_attr_cmd_t *report_attr_cmd) {
  textReports++;
  textReportedAt = ++airEvents;
  lastReport = *report_attr_cmd;
  return true;
}

bool ZigbeeAnalog::setAnalogInput(float analog) {
  wroteValue = analog;
  return true;
}

bool ZigbeeAnalog::reportAnalogInput() {
  valueReports++;
  valueReportedAt = ++airEvents;
  return true;
}

static int fails = 0;
static void check(const char *what, bool ok) {
  printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) fails++;
}

static void reset() {
  clusterMissing = addAttrFails = updateFails = false;
  askedForCluster = 0xFFFF;
  attrsAdded = attrsUpdated = 0;
  createdCluster = createdAttr = updatedAttr = 0;
  createdType = createdAccess = createdLenByte = updatedLenByte = 0;
  createdValue[0] = updatedValue[0] = '\0';
  airEvents = textReportedAt = valueReportedAt = 0;
  textReports = valueReports = 0;
  wroteValue = NAN;
  Zigbee.up = true;
}

// Fills the stack the report below is built in with a pattern, so a field the sketch
// leaves unset reads here as garbage rather than as the zero a fresh host stack happens
// to hold. Not 0xFF: 0xFFFF is a value the sketch has a legitimate reason to send, and
// a poison that can be mistaken for the right answer proves nothing.
static void poisonStack() {
  volatile uint8_t junk[4096];
  memset((void *)junk, 0x5A, sizeof(junk));
  (void)junk[0];
}

int main() {
  printf("the version on the console and the version on the air are one\n");
  // FW_VERSION and FW_VERSION_NUMBER are both built from the three defines in
  // config.h, so they cannot disagree - unless somebody replaces one of them with a
  // literal, which is exactly what this catches.
  int major = 0, minor = 0, patch = 0;
  check("FW_VERSION is three numbers", sscanf(FW_VERSION, "%d.%d.%d", &major, &minor, &patch) == 3);
  check("FW_VERSION_NUMBER says the same", FW_VERSION_NUMBER == major * 10000 + minor * 100 + patch);
  check("and the string fits on the air", strlen(FW_VERSION) <= VERSION_TEXT_MAX);

  printf("the build id in the basic cluster\n");
  reset();
  SwBuildAnalog basic(EP_VERSION);
  check("added", basic.addSoftwareBuildId("2.0.0"));
  check("to the basic cluster", askedForCluster == ESP_ZB_ZCL_CLUSTER_ID_BASIC
                                  && createdCluster == ESP_ZB_ZCL_CLUSTER_ID_BASIC);
  check("as SWBuildID", createdAttr == VERSION_SW_BUILD_ID_ATTR && VERSION_SW_BUILD_ID_ATTR == 0x4000);
  check("a character string", createdType == ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING);
  // Read-only and nothing else: Basic attributes are read during the interview, not
  // reported, so asking for reporting access would be asking for something unused.
  check("read-only, not reportable", createdAccess == ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY);
  check("length byte matches the string", createdLenByte == strlen("2.0.0"));
  check("and the string is the version", strcmp(createdValue, "2.0.0") == 0);
  check("nothing padded onto it", strlen(createdValue) == strlen("2.0.0"));

  printf("the build id when the stack will not take it\n");
  reset();
  clusterMissing = true;
  check("no basic cluster -> refused", !basic.addSoftwareBuildId("2.0.0"));
  check("no basic cluster -> nothing created", attrsAdded == 0);

  // An SDK that creates SWBuildID with the rest of the Basic cluster refuses to add it
  // twice, and then the value is what has to be set instead - so the attribute ends up
  // holding this build's version either way.
  reset();
  addAttrFails = true;
  check("attribute already there -> updated instead", basic.addSoftwareBuildId("2.0.0"));
  check("the same value, the same attribute", attrsUpdated == 1 && updatedAttr == VERSION_SW_BUILD_ID_ATTR
                                               && strcmp(updatedValue, "2.0.0") == 0);
  check("with a length byte of its own", updatedLenByte == strlen("2.0.0"));

  reset();
  addAttrFails = updateFails = true;
  check("neither works -> refused", !basic.addSoftwareBuildId("2.0.0"));

  // A version too long to fit is refused outright rather than cut, because a cut
  // version reads as a different version: "2.0" is a plausible answer and a wrong one.
  reset();
  char exact[VERSION_TEXT_MAX + 1];
  memset(exact, 'x', VERSION_TEXT_MAX);
  exact[VERSION_TEXT_MAX] = '\0';
  check("the longest string that fits is taken", basic.addSoftwareBuildId(exact));
  check("whole, not cut", createdLenByte == VERSION_TEXT_MAX && strcmp(createdValue, exact) == 0);
  reset();
  char tooLong[VERSION_TEXT_MAX + 2];
  memset(tooLong, 'x', VERSION_TEXT_MAX + 1);
  tooLong[VERSION_TEXT_MAX + 1] = '\0';
  check("one character more -> refused", !basic.addSoftwareBuildId(tooLong));
  check("and nothing created", attrsAdded == 0);

  printf("the version text on the endpoint\n");
  reset();
  ZbVersion version(EP_VERSION);
  check("added", version.addText("2.0.0"));
  check("to the analog input cluster", createdCluster == ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT);
  check("as the text attribute", createdAttr == VERSION_TEXT_ATTR_ID);
  check("a character string", createdType == ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING);
  // Reportable, unlike SWBuildID above: publish() sends it after a join instead of
  // leaving the expose empty until somebody presses read.
  check("read-only and reportable",
        createdAccess == (ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING));
  // The mirror pads its line to the full length because a longer one comes later; a
  // version cannot change without a reflash, so there is nothing to make room for.
  check("created at its own length, unpadded", createdLenByte == strlen("2.0.0")
                                                 && strcmp(createdValue, "2.0.0") == 0);

  printf("publishing it\n");
  reset();
  poisonStack();
  version.publish(FW_VERSION_NUMBER);
  check("the number is written", wroteValue == (float)FW_VERSION_NUMBER);
  check("both go out, once each", textReports == 1 && valueReports == 1);
  check("the text before the number", textReportedAt < valueReportedAt);
  check("the text is reported from this endpoint", lastReport.zcl_basic_cmd.src_endpoint == EP_VERSION);
  check("as the text attribute", lastReport.attributeID == VERSION_TEXT_ATTR_ID);
  check("of the analog input cluster", lastReport.clusterID == ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT);
  check("to whoever is bound", lastReport.address_mode == ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT);
  check("towards the client", lastReport.direction == ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI);
  // The attribute sits in the range ZCL reserves for manufacturers but is added with
  // the non-manufacturer call, so that is the key it has to be looked up by. The core's
  // own helpers leave this field alone - arduino-esp32#12917 - which is why the sketch
  // builds the command itself and why this is checked at all.
  check("under no manufacturer code", lastReport.manuf_code == ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC);
  check("not a manufacturer specific report", lastReport.manuf_specific == 0);
  check("no default response asked for", lastReport.dis_default_resp == 0);

  printf("publishing when there is nobody to publish to\n");
  reset();
  Zigbee.up = false;
  version.publish(FW_VERSION_NUMBER);
  check("off the air -> nothing written", std::isnan(wroteValue));
  check("off the air -> nothing reported", textReports == 0 && valueReports == 0);
  // Nothing is held back for later either: the next join publishes it again, and the
  // answer cannot have changed in between.
  Zigbee.up = true;
  version.publish(FW_VERSION_NUMBER);
  check("back on the air -> sent again", textReports == 1 && valueReports == 1);

  printf("publishing without the text attribute\n");
  reset();
  ZbVersion numberOnly(EP_VERSION);
  clusterMissing = true;
  check("no cluster -> no text attribute", !numberOnly.addText("2.0.0"));
  clusterMissing = false;
  numberOnly.publish(FW_VERSION_NUMBER);
  // The number needs none of this: it is the cluster's own value, which is also why it
  // is the half that works without a converter.
  check("the number still goes out", valueReports == 1 && wroteValue == (float)FW_VERSION_NUMBER);
  check("and no text is reported", textReports == 0);

  printf("\n%s\n", fails ? "FAILURES" : "ALL PASS");
  return fails ? 1 : 0;
}
