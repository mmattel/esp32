// Stub of the parts of the Zigbee stack that zb_link.cpp touches.
//
// The types and prototypes are copied verbatim from the headers the Arduino core
// ships (esp32-arduino-libs, espressif__esp-zigbee-lib/include/nwk/esp_zigbee_nwk.h
// and esp_zigbee_core.h), so a change in how the sketch spells them shows up here
// as a compile error rather than only on the board.

#pragma once
#include <Arduino.h>

typedef int esp_err_t;
#define ESP_OK 0

typedef uint8_t esp_zb_ieee_addr_t[8];

#define ESP_ZB_NWK_INFO_ITERATOR_INIT 0
#define ESP_ZB_NWK_INFO_ITERATOR_EOT 0xFFFF
typedef uint16_t esp_zb_nwk_info_iterator_t;

typedef enum {
  ESP_ZB_NWK_RELATIONSHIP_PARENT = 0U,
  ESP_ZB_NWK_RELATIONSHIP_CHILD = 1U,
  ESP_ZB_NWK_RELATIONSHIP_SIBLING = 2U,
  ESP_ZB_NWK_RELATIONSHIP_OTHERS = 3U,
} esp_zb_nwk_relationship_t;

typedef struct esp_zb_nwk_neighbor_info_s {
  esp_zb_ieee_addr_t ieee_addr;
  uint16_t short_addr;
  uint8_t device_type;
  uint8_t depth;
  uint8_t rx_on_when_idle;
  uint8_t relationship;
  uint8_t lqi;
  int8_t rssi;
  uint8_t outgoing_cost;
  uint8_t age;
  uint32_t device_timeout;
  uint32_t timeout_counter;
} esp_zb_nwk_neighbor_info_t;

// Defined by the test: iterates a table it sets up itself.
esp_err_t esp_zb_nwk_get_next_neighbor(esp_zb_nwk_info_iterator_t *iterator, esp_zb_nwk_neighbor_info_t *nbr_info);

// ZDO IEEE address request, as esp_zigbee_zdo_command.h declares it. The response
// type is only ever held as a pointer.
typedef enum { ESP_ZB_ZDP_STATUS_SUCCESS = 0x00 } esp_zb_zdp_status_t;
typedef struct esp_zb_zdo_ieee_addr_rsp_s esp_zb_zdo_ieee_addr_rsp_t;
typedef struct esp_zb_zdo_ieee_addr_req_param_s {
  uint16_t dst_nwk_addr;
  uint16_t addr_of_interest;
  uint8_t request_type;
  uint8_t start_index;
} esp_zb_zdo_ieee_addr_req_param_t;
typedef void (*esp_zb_zdo_ieee_addr_callback_t)(esp_zb_zdp_status_t zdo_status, esp_zb_zdo_ieee_addr_rsp_t *resp,
                                                void *user_ctx);
// APSDE-DATA.indication, field for field as esp_zigbee_aps.h declares it.
typedef struct esp_zb_apsde_data_ind_s {
  uint8_t status;
  uint8_t dst_addr_mode;
  uint16_t dst_short_addr;
  uint8_t dst_endpoint;
  uint8_t src_addr_mode;
  uint16_t src_short_addr;
  uint8_t src_endpoint;
  uint16_t profile_id;
  uint16_t cluster_id;
  uint32_t asdu_length;
  uint8_t *asdu;
  uint8_t security_status;
  int lqi;
  int rx_time;
} esp_zb_apsde_data_ind_t;
typedef bool (*esp_zb_apsde_data_indication_callback_t)(esp_zb_apsde_data_ind_t ind);
// Defined by the test: keeps the handler, so the test can feed it frames.
void esp_zb_aps_data_indication_handler_register(esp_zb_apsde_data_indication_callback_t cb);

// The radio driver's last received frame (esp_ieee802154.h), set by the test.
uint8_t esp_ieee802154_get_recent_lqi(void);
int8_t esp_ieee802154_get_recent_rssi(void);

// Defined by the test: records the request.
void esp_zb_zdo_ieee_addr_req(esp_zb_zdo_ieee_addr_req_param_t *cmd_req, esp_zb_zdo_ieee_addr_callback_t user_cb,
                              void *user_ctx);

// Defined by the test: records whether the lock was taken and released.
bool esp_zb_lock_acquire(TickType_t block_ticks);
void esp_zb_lock_release(void);

struct ZigbeeCoreStub {
  bool up = true;
  bool started() {
    return up;
  }
};
static ZigbeeCoreStub Zigbee;
