// Host tests for the console mirror: which lines reach the air, what the text
// attribute holds when they do, and what happens to a line printed while the
// device is off the air.
//
// Arduino.h and Zigbee.h in this directory are stubs. The stub stores a written
// string the way the stack does - it copies as many bytes as the string's leading
// length byte says - so these tests read back the bytes a coordinator would get,
// padding and all, rather than the sketch's own buffer.

#include "Arduino.h"
#include "../../NanoH2_DS18B20_Zigbee/zb_mirror.cpp"
#include <cassert>

uint32_t hostMillis = 0;

// The mirror logEvent() writes to, as the sketch defines it beside its other
// endpoints. The tests that are not about logEvent() use a fresh local one.
ZbMirror zbMirror(EP_MIRROR);

// What the stack was handed.
static int textWrites = 0;                       // setClusterAttribute() calls
static uint16_t wroteCluster = 0, wroteAttr = 0;  //
static uint8_t wroteRole = 0xFF;                 //
static uint8_t wroteLenByte = 0;                 // the string's leading length byte
static char wroteText[MIRROR_TEXT_LEN + 2] = "";  // the string itself, as the stack would store it
static esp_zb_zcl_status_t writeStatus = ESP_ZB_ZCL_STATUS_SUCCESS;

static int textReports = 0;
static esp_zb_zcl_report_attr_cmd_t lastReport;

static float wroteValue = NAN;
static int valueReports = 0;

// What addText() created.
static bool clusterMissing = false;  // the analog input cluster cannot be found
static bool addAttrFails = false;    // the attribute cannot be added to it
static int attrsAdded = 0;
static uint16_t createdAttr = 0;
static uint8_t createdType = 0, createdAccess = 0, createdLenByte = 0;
static char createdValue[MIRROR_TEXT_LEN + 2] = "";

const char *esp_err_to_name(esp_err_t) {
  return "ESP_ERR (stub)";
}
const char *esp_zb_zcl_status_to_name(esp_zb_zcl_status_t) {
  return "ZCL status (stub)";
}

esp_zb_attribute_list_t *esp_zb_cluster_list_get_cluster(esp_zb_cluster_list_t *, uint16_t, uint8_t) {
  // A non-null pointer is all the sketch does with it: it hands it straight back
  // to esp_zb_cluster_add_attr(), which is a stub as well.
  static int placeholder = 0;
  return clusterMissing ? nullptr : (esp_zb_attribute_list_t *)&placeholder;
}

esp_err_t esp_zb_cluster_add_attr(esp_zb_attribute_list_t *, uint16_t, uint16_t attr_id, uint8_t attr_type,
                                  uint8_t attr_access, void *value_p) {
  if (addAttrFails) {
    return ESP_ERR_INVALID_STATE;
  }
  const char *zcl = (const char *)value_p;
  attrsAdded++;
  createdAttr = attr_id;
  createdType = attr_type;
  createdAccess = attr_access;
  createdLenByte = (uint8_t)zcl[0];
  memcpy(createdValue, zcl + 1, createdLenByte);
  createdValue[createdLenByte] = '\0';
  return ESP_OK;
}

esp_zb_zcl_status_t ZigbeeEP::setClusterAttribute(uint16_t cluster_id, uint8_t cluster_role, uint16_t attr_id,
                                                 void *value, bool) {
  const char *zcl = (const char *)value;
  textWrites++;
  wroteCluster = cluster_id;
  wroteRole = cluster_role;
  wroteAttr = attr_id;
  // As much as the length byte says, which is the point of writing it padded: a
  // string written short over a long one would leave the tail of the old line
  // behind, and this is where that would show.
  wroteLenByte = (uint8_t)zcl[0];
  memcpy(wroteText, zcl + 1, wroteLenByte);
  wroteText[wroteLenByte] = '\0';
  return writeStatus;
}

bool ZigbeeEP::reportClusterAttribute(esp_zb_zcl_report_attr_cmd_t *report_attr_cmd) {
  textReports++;
  lastReport = *report_attr_cmd;
  return true;
}

bool ZigbeeAnalog::setAnalogInput(float analog) {
  wroteValue = analog;
  return true;
}

bool ZigbeeAnalog::reportAnalogInput() {
  valueReports++;
  return true;
}

static int fails = 0;
static void check(const char *what, bool ok) {
  printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) fails++;
}

static void reset() {
  textWrites = textReports = valueReports = attrsAdded = 0;
  wroteText[0] = createdValue[0] = '\0';
  wroteLenByte = createdLenByte = 0;
  wroteValue = NAN;
  writeStatus = ESP_ZB_ZCL_STATUS_SUCCESS;
  clusterMissing = addAttrFails = false;
  hostMillis = 0;
  Zigbee.up = true;
  Serial.lines = 0;
  Serial.last[0] = '\0';
}

// Fills the stack the next calls build their frames in with a pattern, so a field
// the sketch leaves unset is read here as garbage rather than as the zero a fresh
// host stack happens to hold - which is what lets the checks below tell a field that
// was set deliberately from one that was not set at all. The pattern is deliberately
// not 0xFF: 0xFFFF is a value the sketch has a legitimate reason to send, and a
// poison that can be mistaken for the right answer proves nothing.
static void poisonStack() {
  volatile uint8_t junk[4096];
  memset((void *)junk, 0xA5, sizeof(junk));
}

// What a receiver's trim() leaves of the string that was written - see
// "Showing the mirrored line" in the sketch README.
static const char *trimmed() {
  static char out[MIRROR_TEXT_LEN + 2];
  strcpy(out, wroteText);
  size_t n = strlen(out);
  while (n > 0 && out[n - 1] == ' ') {
    out[--n] = '\0';
  }
  return out;
}

// Everything past the line itself has to be spaces, not leftovers and not zeros.
static bool paddedWith(const char *line) {
  if (strlen(wroteText) != MIRROR_TEXT_LEN || strncmp(wroteText, line, strlen(line)) != 0) {
    return false;
  }
  for (size_t i = strlen(line); i < MIRROR_TEXT_LEN; i++) {
    if (wroteText[i] != ' ') {
      return false;
    }
  }
  return true;
}

int main() {
  printf("the attribute the endpoint is built with\n");
  reset();
  {
    ZbMirror m(EP_MIRROR);
    check("addText() succeeds", m.addText());
    check("one attribute added", attrsAdded == 1);
    check("in the manufacturer range, as configured", createdAttr == MIRROR_TEXT_ATTR_ID);
    check("a character string", createdType == ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING);
    check("read-only and reportable, never writable",
          createdAccess == (ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING));
    // The stack sizes the attribute from the value it is created with, so the
    // placeholder has to be the longest string that will ever be written.
    check("created at full length", createdLenByte == MIRROR_TEXT_LEN);
    check("created as spaces", strlen(createdValue) == MIRROR_TEXT_LEN
                                 && strspn(createdValue, " ") == MIRROR_TEXT_LEN);

    reset();
    clusterMissing = true;
    check("no analog input cluster -> addText() fails", !m.addText());
    reset();
    addAttrFails = true;
    check("stack refuses the attribute -> addText() fails", !m.addText());
  }

  printf("a line on the air\n");
  reset();
  {
    ZbMirror m(EP_MIRROR);
    m.addText();
    poisonStack();
    m.mirror("Zigbee connected");
    check("the text was written", textWrites == 1);
    check("to the analog input cluster, server side",
          wroteCluster == ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT && wroteRole == ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    check("to the text attribute", wroteAttr == MIRROR_TEXT_ATTR_ID);
    check("padded to full length", wroteLenByte == MIRROR_TEXT_LEN && paddedWith("Zigbee connected"));
    check("a trim() away from the printed line", strcmp(trimmed(), "Zigbee connected") == 0);
    check("the value is the line count", wroteValue == 1.0f);
    check("both were reported", textReports == 1 && valueReports == 1);
    // Addressed through the binding table: no destination, so it goes to whoever
    // is bound to the cluster. An address of our own would go nowhere.
    check("reported to whoever is bound",
          lastReport.address_mode == ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT);
    check("reported from this endpoint", lastReport.zcl_basic_cmd.src_endpoint == EP_MIRROR);
    check("reported to the client side", lastReport.direction == ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI);
    check("reported as the standard cluster it lives in",
          lastReport.clusterID == ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT && lastReport.attributeID == MIRROR_TEXT_ATTR_ID);
    check("not flagged manufacturer specific", lastReport.manuf_specific == 0);
    // The key the attribute is looked up by, and the one field the core's own report
    // helpers never set. addText() adds the attribute with the non-manufacturer
    // variant of add_attr, so the report has to ask for it under no manufacturer -
    // asking under any other code is asking for an attribute that does not exist.
    // poisonStack() above is what makes an unset field visible here.
    check("asked for under no manufacturer code",
          lastReport.manuf_code == ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC);
  }

  printf("only what changed, and counted as it comes\n");
  reset();
  {
    ZbMirror m(EP_MIRROR);
    m.addText();
    m.mirror("slot 1 (28-0b6b): read failed");
    m.mirror("slot 1 (28-0b6b): read failed");
    m.mirror("slot 1 (28-0b6b): read failed");
    check("a repeated line costs one report, not three", textReports == 1 && valueReports == 1);
    check("and does not move the count", wroteValue == 1.0f);
    m.mirror("1-Wire: no device responded to CONVERT T");
    check("a new line is reported", textReports == 2);
    check("with the next number", wroteValue == 2.0f);
    m.mirror("slot 1 (28-0b6b): read failed");
    check("the line before last is new again", textReports == 3 && wroteValue == 3.0f);
  }

  printf("what the console puts in front of a line\n");
  reset();
  {
    ZbMirror m(EP_MIRROR);
    m.addText();
    // The console indents a line under the header it belongs to; the mirror
    // carries one line on its own, where an indent means nothing.
    m.mirror("  slot 2 (28-0b6c) is configured but missing");
    check("leading spaces dropped", strcmp(trimmed(), "slot 2 (28-0b6c) is configured but missing") == 0);

    char longLine[200];
    memset(longLine, 'x', sizeof(longLine) - 1);
    longLine[sizeof(longLine) - 1] = '\0';
    m.mirror(longLine);
    check("a longer line is cut to the configured length",
          wroteLenByte == MIRROR_TEXT_LEN && strlen(trimmed()) == MIRROR_TEXT_LEN);
    check("cut, not overrun", strncmp(trimmed(), longLine, MIRROR_TEXT_LEN) == 0);
  }

  printf("off the air\n");
  reset();
  {
    ZbMirror m(EP_MIRROR);
    m.addText();
    Zigbee.up = false;
    m.mirror("Zigbee link lost");
    check("nothing written while disconnected", textWrites == 0);
    check("nothing reported while disconnected", textReports == 0 && valueReports == 0);

    // The line is still pending: it was never delivered, so it is not the line
    // the coordinator has, and offering it again once the radio is back sends it.
    Zigbee.up = true;
    m.mirror("Zigbee link lost");
    check("the pending line goes out once back on the air", textReports == 1);
    // Every line the mirror was given is counted, delivered or not - which is
    // what makes a gap in the numbers honest: the line arrives as number two,
    // saying one line went by while the device was off the air.
    check("the attempt off the air was counted too", wroteValue == 2.0f);
  }

  printf("a join\n");
  reset();
  {
    ZbMirror m(EP_MIRROR);
    m.addText();
    m.mirror("Zigbee connected");
    m.mirror("Zigbee connected");
    check("the deadband holds within one join", textReports == 1);
    // Whatever was published belonged to the previous network, or to nobody at
    // all, so onZigbeeConnected() forgets it before printing the join line.
    m.forgetPublished();
    m.mirror("Zigbee connected");
    check("forgetPublished() sends the same line again", textReports == 2);
    check("and it is a new line as far as the count goes", wroteValue == 2.0f);
  }

  printf("the heartbeat\n");
  reset();
  {
    ZbMirror m(EP_MIRROR);
    m.addText();
    m.handleReports();
    check("nothing printed yet -> nothing repeated", textReports == 0);

    m.mirror("Zigbee connected");
    hostMillis = MIRROR_REPORT_HEARTBEAT_S * 1000UL - 1;
    m.handleReports();
    check("a moment early -> not yet", textReports == 1);
    hostMillis = MIRROR_REPORT_HEARTBEAT_S * 1000UL;
    m.handleReports();
    check("on time -> repeated", textReports == 2);
    check("the same line, still padded", paddedWith("Zigbee connected"));
    check("the count is unchanged by a repeat", wroteValue == 1.0f);

    // The interval runs from the last report, not from the last new line.
    hostMillis += MIRROR_REPORT_HEARTBEAT_S * 1000UL - 1;
    m.handleReports();
    check("the interval restarts at the repeat", textReports == 2);
    hostMillis += 1;
    m.handleReports();
    check("and comes round again", textReports == 3);

    Zigbee.up = false;
    hostMillis += MIRROR_REPORT_HEARTBEAT_S * 1000UL;
    m.handleReports();
    check("off the air -> nothing repeated", textReports == 3);
  }

  printf("without the text attribute\n");
  reset();
  {
    ZbMirror m(EP_MIRROR);
    clusterMissing = true;
    m.addText();  // fails, and the sketch treats that as a warning, not as fatal
    m.mirror("Zigbee connected");
    check("no text is written", textWrites == 0);
    check("no text is reported", textReports == 0);
    check("the line count still goes out", valueReports == 1 && wroteValue == 1.0f);
  }

  printf("a write the stack refuses\n");
  reset();
  {
    ZbMirror m(EP_MIRROR);
    m.addText();
    writeStatus = ESP_ZB_ZCL_STATUS_FAIL;
    m.mirror("FATAL: 1-Wire bus not responding");
    // log_e() has said so by now. The count goes out regardless, so a coordinator
    // sees that something was printed even when the text of it did not make it,
    // and the next line - or the heartbeat - writes the attribute again.
    check("the line count still goes out", valueReports == 1 && wroteValue == 1.0f);
    check("a text that was not written is not reported", textReports == 0);
    // On the board log_e() above prints nothing - Core Debug Level is "None" - so
    // without this line a mirror that has stopped working and a coordinator that
    // ignores it look the same from the console. mirror() itself prints nothing, so
    // every line counted here is the mirror talking about itself.
    check("the console is told", Serial.lines == 1 && strstr(Serial.last, "cannot be sent") != nullptr);
    m.mirror("FATAL: 1-Wire bus still not responding");
    check("a spell of it costs one line, not one per line", Serial.lines == 1);
    writeStatus = ESP_ZB_ZCL_STATUS_SUCCESS;
    m.mirror("1-Wire bus responding again");
    check("the recovery is said as well", Serial.lines == 2 && strstr(Serial.last, "sending again") != nullptr);
  }

  printf("logEvent(): printed and mirrored, same line\n");
  reset();
  {
    zbMirror.addText();
    zbMirror.forgetPublished();
    logEvent("EP %u: sensor id attribute unavailable", 11);
    check("formatted once, for both", strcmp(trimmed(), "EP 11: sensor id attribute unavailable") == 0);
    check("reported", textReports == 1);

    // Longer than logEvent()'s own buffer: the console line is cut, and what the
    // mirror keeps of it is cut again. Neither may run off the end.
    char huge[400];
    memset(huge, 'y', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\0';
    logEvent("%s", huge);
    check("a line past the console buffer is cut, not overrun",
          wroteLenByte == MIRROR_TEXT_LEN && strspn(trimmed(), "y") == MIRROR_TEXT_LEN);
  }

  printf("\n%s\n", fails ? "FAILURES" : "ALL PASS");
  return fails ? 1 : 0;
}
