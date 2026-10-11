// Stub of the parts of the Zigbee stack that zb_version.cpp touches.
//
// The types, prototypes and access levels are those the Arduino core and the SDK
// ship (libraries/Zigbee/src/ep/ZigbeeEP.h and ZigbeeAnalog.h, esp_zigbee_cluster.h,
// esp_zigbee_zcl_command.h), so a change in how the sketch spells them shows up here
// as a compile error rather than only on the board. _cluster_list in particular stays
// protected, as it is upstream: that is the whole reason SwBuildAnalog is a subclass,
// and if the core ever makes it public this suite says so.

#pragma once
#include <Arduino.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 259
const char *esp_err_to_name(esp_err_t code);

#define ESP_ZB_ZCL_CLUSTER_ID_BASIC 0x0000
#define ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT 0x000c
#define ESP_ZB_ZCL_CLUSTER_SERVER_ROLE 0x00
#define ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING 0x42
#define ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY 0x01
#define ESP_ZB_ZCL_ATTR_ACCESS_REPORTING 0x04
#define ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID 0x0055
// "No manufacturer", as the SDK generation the core ships spells it; the next one
// keeps the name and changes the number, which is why the sketch uses the name.
#define ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC 0xFFFFU

// Opaque in the SDK too: only ever held as a pointer.
typedef struct esp_zb_attribute_list_s esp_zb_attribute_list_t;
typedef struct esp_zb_cluster_list_s esp_zb_cluster_list_t;

// All three defined by the test: it hands out stand-in clusters, or refuses to, and
// records what was created in one.
esp_zb_attribute_list_t *esp_zb_cluster_list_get_cluster(esp_zb_cluster_list_t *cluster_list, uint16_t cluster_id,
                                                        uint8_t role_mask);
esp_err_t esp_zb_cluster_add_attr(esp_zb_attribute_list_t *attr_list, uint16_t cluster_id, uint16_t attr_id,
                                  uint8_t attr_type, uint8_t attr_access, void *value_p);
esp_err_t esp_zb_cluster_update_attr(esp_zb_attribute_list_t *attr_list, uint16_t attr_id, void *value_p);

class ZigbeeEP {
public:
  explicit ZigbeeEP(uint8_t endpoint) : _endpoint(endpoint) {}
  virtual ~ZigbeeEP() {}

protected:
  uint8_t _endpoint;
  esp_zb_cluster_list_t *_cluster_list = nullptr;  // set by the core at registration
};

class ZigbeeAnalog : public ZigbeeEP {
public:
  explicit ZigbeeAnalog(uint8_t endpoint) : ZigbeeEP(endpoint) {}
  // Defined by the test: records the value. Reports go through zbReport(), which
  // the test defines as well - see zb_report.h.
  bool setAnalogInput(float analog);
};

struct ZigbeeCoreStub {
  bool up = true;
  bool connected() {
    return up;
  }
};
static ZigbeeCoreStub Zigbee;
