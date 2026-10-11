// Stub of the parts of the Zigbee stack that zb_report.cpp touches.
//
// The report command and the reporting info carry the fields the SDK declares
// (esp_zigbee_zcl_command.h, esp_zigbee_type.h), in its order, bitfields included,
// so a field the sketch spells differently from the SDK is a compile error here and
// not only on the board. The unions the sketch never touches are left out.

#pragma once
#include <Arduino.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_TIMEOUT 0x107
const char *esp_err_to_name(esp_err_t code);

typedef uint32_t TickType_t;
#define portMAX_DELAY 0xFFFFFFFFu

#define ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT 0x000c
#define ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT 0x0402
#define ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID 0x0055
#define ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID 0x0000
#define ESP_ZB_ZCL_CLUSTER_SERVER_ROLE 0x01
#define ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI 0x01
#define ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT 0x00
#define ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT 0x02
#define ESP_ZB_AF_HA_PROFILE_ID 0x0104
// "No manufacturer", as the SDK generation the core ships spells it; the next one
// keeps the name and changes the number, which is why the sketch uses the name.
#define ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC 0xFFFFU

typedef struct esp_zb_zcl_basic_cmd_s {
  union {
    uint16_t addr_short;
    uint8_t addr_long[8];
  } dst_addr_u;
  uint8_t dst_endpoint;
  uint8_t src_endpoint;
} esp_zb_zcl_basic_cmd_t;

typedef struct esp_zb_zcl_report_attr_cmd_s {
  esp_zb_zcl_basic_cmd_t zcl_basic_cmd;
  uint8_t address_mode;
  uint16_t clusterID;
  struct {
    uint8_t manuf_specific : 2;
    uint8_t direction : 1;
    uint8_t dis_default_resp : 1;
  };
  uint16_t manuf_code;
  uint16_t attributeID;
} esp_zb_zcl_report_attr_cmd_t;

typedef struct esp_zb_zcl_reporting_info_s {
  uint8_t direction;
  uint8_t ep;
  uint16_t cluster_id;
  uint8_t cluster_role;
  uint16_t attr_id;
  uint8_t flags;
  uint64_t run_time;
  union {
    struct {
      uint16_t min_interval;
      uint16_t max_interval;
      uint16_t def_min_interval;
      uint16_t def_max_interval;
    } send_info;
  } u;
  struct {
    uint16_t short_addr;
    uint8_t endpoint;
    uint16_t profile_id;
  } dst;
  uint16_t manuf_code;
} esp_zb_zcl_reporting_info_t;

typedef struct esp_zb_zcl_command_send_status_s {
  esp_err_t status;
  uint8_t tsn;
  uint8_t dst_endpoint;
  uint8_t src_endpoint;
} esp_zb_zcl_command_send_status_message_t;
typedef void (*esp_zb_zcl_command_send_status_callback_t)(esp_zb_zcl_command_send_status_message_t message);

// All defined by the test.
bool esp_zb_lock_acquire(TickType_t block_ticks);
void esp_zb_lock_release();
esp_err_t esp_zb_zcl_report_attr_cmd_req(esp_zb_zcl_report_attr_cmd_t *cmd_req);
void esp_zb_zcl_update_reporting_info(esp_zb_zcl_reporting_info_t *report_info);
void esp_zb_zcl_command_send_status_handler_register(esp_zb_zcl_command_send_status_callback_t cb);

struct ZigbeeCoreStub {
  bool running = true;
  bool up = true;
  bool started() {
    return running;
  }
  bool connected() {
    return up;
  }
};
static ZigbeeCoreStub Zigbee;
