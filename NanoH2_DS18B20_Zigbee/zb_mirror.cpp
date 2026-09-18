#include "zb_mirror.h"

#include <stdarg.h>

// Room for a whole console line. The mirror keeps MIRROR_TEXT_LEN of it, but the
// console gets everything, and the longest line that goes through logEvent() today
// is around sixty characters - so this is the limit of what can be printed, not the
// limit of what is printed.
static constexpr size_t LINE_MAX = 160;

void logEvent(const char *fmt, ...) {
  char line[LINE_MAX];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);

  Serial.println(line);  // println ends the line with \r\n, like the printf calls do
  zbMirror.mirror(line);
}

void ZbMirror::mirror(const char *line) {
  if (!ZB_MIRROR_ENDPOINT) {
    return;  // no endpoint in this build, so the line stays on the console
  }

  // The console indents a line under the header it belongs to. The mirror carries
  // one line on its own, where an indent means nothing.
  while (*line == ' ') {
    line++;
  }
  strncpy(_text, line, MIRROR_TEXT_LEN);
  _text[MIRROR_TEXT_LEN] = '\0';

  // A line the coordinator already has changes nothing on the air. This is the
  // same deadband the temperatures and the link values use: a read that fails
  // every interval, or a slot that stays missing, then costs one report instead of
  // one per interval. The heartbeat still repeats it.
  if (strcmp(_text, _published) == 0) {
    return;
  }

  // Counts every line the mirror was given, whether or not the radio could deliver
  // it. So a gap in the sequence is honest: it says lines were printed while the
  // device was off the air.
  _sequence++;
  publish();
}

void ZbMirror::forgetPublished() {
  _published[0] = '\0';
}

void ZbMirror::handleReports() {
  if (!ZB_MIRROR_ENDPOINT || MIRROR_REPORT_HEARTBEAT_S == 0) {
    return;
  }
  if (!Zigbee.connected() || _text[0] == '\0') {
    return;  // nobody to repeat it to, or nothing printed yet
  }
  if ((millis() - _lastReportMs) < (uint32_t)MIRROR_REPORT_HEARTBEAT_S * 1000UL) {
    return;
  }
  publish();
}

void ZbMirror::publish() {
  if (!Zigbee.connected()) {
    // Off the air there is nobody to report to, and _published is left alone on
    // purpose: the line is still pending, and the next join sends it.
    return;
  }

  // The text goes first and the sequence number second, so that a coordinator
  // acting on a new number already has the line that number belongs to.
  setText();
  setAnalogInput(_sequence);
  if (_hasText) {
    reportText();
  }
  reportAnalogInput();

  strcpy(_published, _text);
  _lastReportMs = millis();
}

// A ZCL character string is stored with a leading length byte, and the stack sizes
// the attribute from the value it is created with - the same thing
// TempEndpoint::setSensorId() works around. So addText() creates it at its full
// length, all spaces, and every line written afterwards keeps that length, so it
// always fits the room that reserved. The placeholder itself is never seen: the
// first line published is the join, and before the join there is no coordinator to
// read anything.

bool ZbMirror::setText() {
  if (!_hasText) {
    return false;
  }

  // Space padded to the full length on every write, not only on the first, exactly
  // as TempEndpoint::setSensorId() pads the sensor id: the stack copies a string
  // by the length byte it is handed, and how much it copies when a shorter value
  // lands on a longer one is its business, not ours. So the line goes out with
  // trailing spaces, which is a receiver's trim() away - see README.md.
  size_t len = strlen(_text);  // mirror() has already cut this to MIRROR_TEXT_LEN
  char zcl[MIRROR_TEXT_LEN + 2];
  zcl[0] = (char)MIRROR_TEXT_LEN;
  memset(zcl + 1, ' ', MIRROR_TEXT_LEN);
  memcpy(zcl + 1, _text, len);
  zcl[MIRROR_TEXT_LEN + 1] = '\0';

  esp_zb_zcl_status_t status = setClusterAttribute(ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
                                                  ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, MIRROR_TEXT_ATTR_ID, (void *)zcl);
  if (status != ESP_ZB_ZCL_STATUS_SUCCESS) {
    log_e("Failed to update the mirrored line: %s", esp_zb_zcl_status_to_name(status));
    return false;
  }
  return true;
}

bool ZbMirror::reportText() {
  // The library reports the attributes it knows about, not this one, so the report
  // is built here - field for field like ZigbeeAnalog::reportAnalogInput() does.
  // The address mode is what sends it to whoever is bound to the cluster.
  esp_zb_zcl_report_attr_cmd_t cmd;
  cmd.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
  cmd.attributeID = MIRROR_TEXT_ATTR_ID;
  cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
  cmd.clusterID = ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT;
  cmd.zcl_basic_cmd.src_endpoint = _endpoint;
  cmd.manuf_specific = 0x00U;
  cmd.dis_default_resp = 0x00U;

  return reportClusterAttribute(&cmd);
}

bool ZbMirror::addText() {
  esp_zb_attribute_list_t *cluster = esp_zb_cluster_list_get_cluster(
    _cluster_list, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  if (cluster == nullptr) {
    log_e("Failed to get analog input cluster for the mirrored line");
    return false;
  }

  char placeholder[MIRROR_TEXT_LEN + 2];
  placeholder[0] = (char)MIRROR_TEXT_LEN;
  memset(placeholder + 1, ' ', MIRROR_TEXT_LEN);
  placeholder[MIRROR_TEXT_LEN + 1] = '\0';

  // There is no cluster specific add_attr for an attribute the standard does not
  // define, so the generic one is used, and it is the call that says what the
  // attribute is: a character string that can be read and reported, never written.
  esp_err_t ret = esp_zb_cluster_add_attr(cluster, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, MIRROR_TEXT_ATTR_ID,
                                          ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
                                          (void *)placeholder);
  if (ret != ESP_OK) {
    log_w("Failed to add the mirrored line attribute: 0x%x: %s", ret, esp_err_to_name(ret));
    return false;
  }
  _hasText = true;
  return true;
}
