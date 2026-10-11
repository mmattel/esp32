// Host tests for zb_report: what every report looks like on the air, and what the
// console says when reports are refused, lost, or get through again.
//
// Arduino.h and Zigbee.h in this directory are stubs. The stack calls are recorded.

#include "Arduino.h"
#include "../../NanoH2_DS18B20_Zigbee/zb_report.cpp"

uint32_t hostMillis = 0;

static bool lockFails = false;
static int locksHeld = 0;
static esp_err_t reportResult = ESP_OK;
static int reportsRequested = 0;
static esp_zb_zcl_report_attr_cmd_t lastReport;
static esp_zb_zcl_reporting_info_t lastReportingInfo;
static bool sendStatusRegistered = false;

const char *esp_err_to_name(esp_err_t code) {
  return code == ESP_ERR_TIMEOUT ? "ESP_ERR_TIMEOUT" : code == ESP_FAIL ? "ESP_FAIL" : "ESP_ERR (stub)";
}
bool esp_zb_lock_acquire(TickType_t) {
  if (lockFails) {
    return false;
  }
  locksHeld++;
  return true;
}
void esp_zb_lock_release() {
  locksHeld--;
}
esp_err_t esp_zb_zcl_report_attr_cmd_req(esp_zb_zcl_report_attr_cmd_t *cmd_req) {
  reportsRequested++;
  lastReport = *cmd_req;
  return reportResult;
}
static int reportingInfoLocksHeld = -1;  // locks held while the stack was called
void esp_zb_zcl_update_reporting_info(esp_zb_zcl_reporting_info_t *report_info) {
  lastReportingInfo = *report_info;
  reportingInfoLocksHeld = locksHeld;
}
// Must never be called: registering it from the main task, unlocked, as the stack
// starts was the likely trigger of a vPortExitCritical assert, and it never fired
// for a report anyway - see zbReportBegin().
void esp_zb_zcl_command_send_status_handler_register(esp_zb_zcl_command_send_status_callback_t) {
  sendStatusRegistered = true;
}

static int fails = 0;
static void check(const char *what, bool ok) {
  printf("  %-56s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) fails++;
}

// Fills the stack the report is built in with a pattern, so a field the sketch
// leaves unset reads here as garbage rather than as the zero a fresh host stack
// happens to hold.
static void poisonStack() {
  volatile uint8_t junk[4096];
  memset((void *)junk, 0x5A, sizeof(junk));
  (void)junk[0];
}

int main() {
  zbReportBegin();
  printf("starting touches nothing in the stack\n");
  check("no send status callback registered", !sendStatusRegistered);

  printf("every report goes to the coordinator, the same way\n");
  poisonStack();
  bool ok = zbReport(20, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID);
  check("the stack took it", ok && reportsRequested == 1);
  check("under the Zigbee lock, and the lock given back", locksHeld == 0);
  // Not the binding table: that mode silently drops reports on this device.
  check("unicast with a short address and an endpoint", lastReport.address_mode == ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT);
  check("to the coordinator, 0x0000", lastReport.zcl_basic_cmd.dst_addr_u.addr_short == 0x0000);
  check("endpoint 1", lastReport.zcl_basic_cmd.dst_endpoint == 1);
  check("from the endpoint it was asked for", lastReport.zcl_basic_cmd.src_endpoint == 20);
  check("the cluster and attribute it was asked for",
        lastReport.clusterID == ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT
          && lastReport.attributeID == ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID);
  check("towards the client", lastReport.direction == ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI);
  check("not manufacturer specific", lastReport.manuf_specific == 0);
  check("under no manufacturer code", lastReport.manuf_code == ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC);
  // The one that halves the traffic: no Default Response back from the coordinator.
  check("no default response asked for", lastReport.dis_default_resp == 1);

  printf("nothing goes out while the stack is down\n");
  Zigbee.running = false;
  reportsRequested = 0;
  check("refused without asking the stack", !zbReport(12, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, 0x55) && reportsRequested == 0);
  Zigbee.running = true;

  printf("a spell of refusals is two lines, not one per report\n");
  Serial.lines = 0;
  reportResult = ESP_ERR_INVALID_ARG;
  check("refused -> false", !zbReport(14, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, 0xF000));
  check("the console says so, naming the attribute",
        Serial.lines == 1 && strstr(Serial.last, "EP 14") && strstr(Serial.last, "0xF000"));
  zbReport(14, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, 0xF000);
  zbReport(14, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, 0xF000);
  check("and not again while it lasts", Serial.lines == 1);
  // Another endpoint has a spell of its own.
  zbReport(16, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, 0xF000);
  check("another endpoint is told about separately", Serial.lines == 2 && strstr(Serial.last, "EP 16"));
  reportResult = ESP_OK;
  zbReport(14, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, 0xF000);
  check("the recovery is said, with the count",
        Serial.lines == 3 && strstr(Serial.last, "accepted again") && strstr(Serial.last, "3 refused"));
  zbReport(14, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, 0xF000);
  check("and only once", Serial.lines == 3);
  zbReport(16, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, 0xF000);

  printf("two attributes on one endpoint keep separate spells\n");
  // What the mirror does on every line: the text, then the counter, from EP 14.
  // With only the text refused, the counter's success is not the text recovering.
  Serial.lines = 0;
  reportResult = ESP_ERR_INVALID_ARG;
  zbReport(14, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, 0xF000);
  reportResult = ESP_OK;
  zbReport(14, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID);
  check("the text refused is told", Serial.lines == 1 && strstr(Serial.last, "0xF000 refused"));
  reportResult = ESP_ERR_INVALID_ARG;
  zbReport(14, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, 0xF000);
  reportResult = ESP_OK;
  zbReport(14, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID);
  check("the counter's success says nothing about it", Serial.lines == 1);
  zbReport(14, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, 0xF000);
  check("the text's own success ends its spell",
        Serial.lines == 2 && strstr(Serial.last, "0xF000 accepted again") && strstr(Serial.last, "2 refused"));

  printf("a lock that cannot be had is a refusal\n");
  Serial.lines = 0;
  lockFails = true;
  check("refused -> false", !zbReport(12, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, 0x55));
  check("as a timeout", Serial.lines == 1 && strstr(Serial.last, "ESP_ERR_TIMEOUT"));
  lockFails = false;
  zbReport(12, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, 0x55);

  printf("the summary\n");
  // From a clean slate: what happened up to now was counted into the totals, so
  // one summary first takes it out of the next one.
  hostMillis += REPORT_STATS_INTERVAL_S * 1000UL;
  zbReportHandle();
  Serial.lines = 0;
  Serial.last[0] = '\0';
  hostMillis += REPORT_STATS_INTERVAL_S * 1000UL - 1;
  zbReport(20, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, 0);
  zbReport(21, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, 0);
  reportResult = ESP_FAIL;
  zbReport(22, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, 0);
  reportResult = ESP_OK;
  zbReport(22, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, 0);  // ends that spell again
  zbReportHandle();
  check("not before the interval is up", strstr(Serial.last, "reports, last") == nullptr);
  hostMillis += 1;
  zbReportHandle();
  check("then the counts since the last one", strstr(Serial.last, "3 sent, 1 refused") != nullptr);
  check("and nothing about the connection while connected", strstr(Serial.last, "not connected") == nullptr);
  Zigbee.up = false;
  hostMillis += REPORT_STATS_INTERVAL_S * 1000UL;
  zbReportHandle();
  check("zeros are printed too, and offline is said",
        strstr(Serial.last, "0 sent, 0 refused (not connected)") != nullptr);
  Zigbee.up = true;

  printf("a reporting entry for an attribute added by hand\n");
  zbSetReporting(14, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, 0xF000, 1, 3600);
  const esp_zb_zcl_reporting_info_t &ri = lastReportingInfo;
  check("for that endpoint, cluster and attribute",
        ri.ep == 14 && ri.cluster_id == ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT && ri.attr_id == 0xF000);
  check("on the server side, sending to the client",
        ri.cluster_role == ESP_ZB_ZCL_CLUSTER_SERVER_ROLE && ri.direction == ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI);
  check("with the intervals, as the defaults too",
        ri.u.send_info.min_interval == 1 && ri.u.send_info.max_interval == 3600
          && ri.u.send_info.def_min_interval == 1 && ri.u.send_info.def_max_interval == 3600);
  // The one call that once ran without the lock, from applyReporting() on every join
  // - and crashed the stack's task about one boot in nine.
  check("under the Zigbee lock, and the lock given back", reportingInfoLocksHeld == 1 && locksHeld == 0);
  check("under no manufacturer code, in the HA profile",
        ri.manuf_code == ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC && ri.dst.profile_id == ESP_ZB_AF_HA_PROFILE_ID);

  printf("\n%s\n", fails ? "FAILURES" : "ALL PASS");
  return fails ? 1 : 0;
}
