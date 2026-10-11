#include "zb_report.h"
#include "config.h"

#if !defined(REPORT_STATS_INTERVAL_S)
#error "config.h has no REPORT_STATS_INTERVAL_S - update the whole sketch folder from one commit"
#endif

// Indexed by endpoint number. Every endpoint either sketch has is far below this;
// one above it is still reported, it just goes uncounted.
static constexpr uint8_t TRACKED_ENDPOINTS = 64;

struct EndpointStats {
  uint32_t accepted = 0;
  uint32_t refused = 0;
};

static EndpointStats stats[TRACKED_ENDPOINTS];

// The attributes currently being refused, each with the number of refusals so far.
// Keyed by endpoint and attribute, not endpoint alone: the mirror and the version
// each report two attributes from one endpoint, and when only the text is refused,
// the value's success right after it must not read as the text recovering.
// A dozen is far more than can fail at once. A refusal past that is still counted,
// only not printed.
struct RefusalSpell {
  uint8_t ep;
  uint16_t attrId;
  uint32_t count;  // 0: the entry is free
};
static RefusalSpell spells[12];

// Totals as of the last summary, so the next one can say what happened since.
static uint32_t summaryAccepted = 0, summaryRefused = 0;
static uint32_t lastSummaryMs = 0;

static RefusalSpell *findSpell(uint8_t ep, uint16_t attrId) {
  for (RefusalSpell &sp : spells) {
    if (sp.count > 0 && sp.ep == ep && sp.attrId == attrId) {
      return &sp;
    }
  }
  return nullptr;
}

static void noteSubmitted(uint8_t ep, uint16_t attrId, esp_err_t ret) {
  RefusalSpell *spell = findSpell(ep, attrId);
  if (ret == ESP_OK) {
    if (ep < TRACKED_ENDPOINTS) {
      stats[ep].accepted++;
    }
    if (spell) {
      Serial.printf("EP %u: reports of attribute 0x%04X accepted again, %lu refused before" CONSOLE_EOL, ep, attrId,
                    (unsigned long)spell->count);
      spell->count = 0;
    }
    return;
  }
  if (ep < TRACKED_ENDPOINTS) {
    stats[ep].refused++;
  }
  if (spell) {
    spell->count++;
    return;
  }
  Serial.printf("EP %u: report of attribute 0x%04X refused by the stack: %s (%d)" CONSOLE_EOL, ep, attrId,
                esp_err_to_name(ret), (int)ret);
  for (RefusalSpell &sp : spells) {
    if (sp.count == 0) {
      sp = {ep, attrId, 1};
      break;
    }
  }
}

bool zbReport(uint8_t srcEndpoint, uint16_t clusterId, uint16_t attrId) {
  if (!Zigbee.started()) {
    return false;
  }

  // Zeroed first, so nothing in it is left to whatever the stack happened to hold:
  // the named fields below do not cover the padding around the bitfields, and a
  // later SDK can add fields this code has never heard of.
  esp_zb_zcl_report_attr_cmd_t cmd = {};
  cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
  cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;
  cmd.zcl_basic_cmd.dst_endpoint = 1;
  cmd.zcl_basic_cmd.src_endpoint = srcEndpoint;
  cmd.clusterID = clusterId;
  cmd.attributeID = attrId;
  cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
  cmd.manuf_specific = 0x00U;
  cmd.dis_default_resp = 0x01U;  // see zb_report.h
  // The manufacturer the attribute is keyed under, and it is the lookup key: none,
  // for the standard attributes and equally for the hand-added 0xF000 ones, because
  // they are created with esp_zb_cluster_add_attr(), the non-manufacturer variant,
  // even though the ID sits in the range ZCL reserves for manufacturers. The name,
  // not the number: it is 0xFFFF in the SDK generation this builds against and
  // 0x0000 in the next one. The core's own helpers leave this field uninitialised,
  // which only works while the stack ignores it - arduino-esp32 #12917.
  cmd.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;

  esp_err_t ret;
  if (esp_zb_lock_acquire(portMAX_DELAY)) {
    ret = esp_zb_zcl_report_attr_cmd_req(&cmd);
    esp_zb_lock_release();
  } else {
    ret = ESP_ERR_TIMEOUT;
  }
  noteSubmitted(srcEndpoint, attrId, ret);
  return ret == ESP_OK;
}

void zbSetReporting(uint8_t endpoint, uint16_t clusterId, uint16_t attrId, uint16_t minInterval, uint16_t maxInterval) {
  esp_zb_zcl_reporting_info_t ri = {};
  ri.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
  ri.ep = endpoint;
  ri.cluster_id = clusterId;
  ri.cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE;
  ri.attr_id = attrId;
  ri.u.send_info.min_interval = minInterval;
  ri.u.send_info.max_interval = maxInterval;
  ri.u.send_info.def_min_interval = minInterval;
  ri.u.send_info.def_max_interval = maxInterval;
  ri.dst.profile_id = ESP_ZB_AF_HA_PROFILE_ID;
  ri.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;
  // Under the Zigbee lock, as the core's own setClusterReporting() does it. Until
  // v3.2.6 this ran without it, from applyReporting() on every join - while the stack's
  // task was busy with that very join. That was the vPortExitCritical assert seen in
  // the stack's task right after "Zigbee started", about one boot in nine (field logs
  // of 2026-10-11): two tasks inside the stack at once. arduino-esp32 #10879 is the
  // same assert from the same cause, an unlocked call, in the core's scanNetworks().
  if (!esp_zb_lock_acquire(portMAX_DELAY)) {
    return;
  }
  esp_zb_zcl_update_reporting_info(&ri);
  esp_zb_lock_release();
}

// No send status callback: v3.1.2 registered esp_zb_zcl_command_send_status_handler
// to count deliveries, and it never fired for a single report (field log of
// 2026-10-11: "34 sent, 0 delivered, 0 not delivered"). The APS-level alternative,
// esp_zb_aps_data_confirm_handler_register(), hands the application the confirm
// handling for every frame, the stack's own included, which is too much to take on
// for a counter. Whether a report arrived is answered at the coordinator.
void zbReportBegin() {
  lastSummaryMs = millis();
}

void zbReportHandle() {
  if (REPORT_STATS_INTERVAL_S == 0 || (millis() - lastSummaryMs) < (uint32_t)REPORT_STATS_INTERVAL_S * 1000UL) {
    return;
  }
  lastSummaryMs = millis();

  uint32_t accepted = 0, refused = 0;
  for (uint8_t ep = 0; ep < TRACKED_ENDPOINTS; ep++) {
    accepted += stats[ep].accepted;
    refused += stats[ep].refused;
  }
  // Printed whether or not anything was sent: in a capture left running unattended,
  // a summary of zeros still says the device was alive and on the air at that time.
  Serial.printf("reports, last %u s: %lu sent, %lu refused%s" CONSOLE_EOL, (unsigned)REPORT_STATS_INTERVAL_S,
                (unsigned long)(accepted - summaryAccepted), (unsigned long)(refused - summaryRefused),
                Zigbee.connected() ? "" : " (not connected)");
  summaryAccepted = accepted;
  summaryRefused = refused;
}
