#include "zb_temp_endpoint.h"

bool TempEndpoint::reportTemperature() {
  // Direct unicast to coordinator 0x0000 endpoint 1: the binding table mode
  // (ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT) silently fails on this
  // device even when bindings are present - confirmed in v3.0.2.
  esp_zb_zcl_report_attr_cmd_t cmd = {};
  cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
  cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;
  cmd.zcl_basic_cmd.dst_endpoint = 1;
  cmd.attributeID = ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID;
  cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
  cmd.clusterID = ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT;
  cmd.zcl_basic_cmd.src_endpoint = _endpoint;
  cmd.manuf_specific = 0x00U;
  cmd.dis_default_resp = 0x00U;
  cmd.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;
  return reportClusterAttribute(&cmd);
}

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
