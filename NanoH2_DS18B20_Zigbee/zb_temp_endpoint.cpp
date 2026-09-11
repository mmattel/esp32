#include "zb_temp_endpoint.h"

// LocationDescription is a ZCL character string of at most 16 characters,
// stored with a leading length byte. Values are space padded to the full 16 so
// that the stack sizes the attribute at its maximum when it is created: a
// shorter first value would leave no room for a longer one later.
static constexpr uint8_t LOCATION_LEN = 16;

bool TempEndpoint::setSensorId(const char *id) {
  size_t len = strlen(id);
  if (len > LOCATION_LEN) {
    log_e("Sensor id '%s' is longer than %u characters", id, (unsigned)LOCATION_LEN);
    return false;
  }

  char zcl[LOCATION_LEN + 2];
  zcl[0] = (char)LOCATION_LEN;
  memset(zcl + 1, ' ', LOCATION_LEN);
  memcpy(zcl + 1, id, len);
  zcl[LOCATION_LEN + 1] = '\0';

  if (_created) {
    // Once Zigbee.begin() has run, the attribute list belongs to the stack and
    // the value has to be changed through the ZCL layer.
    esp_zb_zcl_status_t status = setClusterAttribute(ESP_ZB_ZCL_CLUSTER_ID_BASIC, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                                    ESP_ZB_ZCL_ATTR_BASIC_LOCATION_DESCRIPTION_ID, (void *)zcl);
    if (status != ESP_ZB_ZCL_STATUS_SUCCESS) {
      log_e("Failed to update sensor id: %s", esp_zb_zcl_status_to_name(status));
      return false;
    }
    return true;
  }

  esp_zb_attribute_list_t *basic_cluster =
    esp_zb_cluster_list_get_cluster(_cluster_list, ESP_ZB_ZCL_CLUSTER_ID_BASIC, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  if (basic_cluster == nullptr) {
    log_e("Failed to get basic cluster for sensor id");
    return false;
  }

  esp_err_t ret =
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_LOCATION_DESCRIPTION_ID, (void *)zcl);
  if (ret != ESP_OK) {
    log_e("Failed to add sensor id to basic cluster: 0x%x: %s", ret, esp_err_to_name(ret));
    return false;
  }
  _created = true;
  return true;
}
