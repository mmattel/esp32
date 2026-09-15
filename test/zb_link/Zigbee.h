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
