#include "zb_link_endpoint.h"

bool LinkAnalog::reportAnalogInput() {
  // Direct unicast to coordinator 0x0000 endpoint 1: the binding table mode
  // (ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT) silently fails on this
  // device even when bindings are present - confirmed in v3.0.2.
  esp_zb_zcl_report_attr_cmd_t cmd = {};
  cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
  cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;
  cmd.zcl_basic_cmd.dst_endpoint = 1;
  cmd.attributeID = ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID;
  cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
  cmd.clusterID = ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT;
  cmd.zcl_basic_cmd.src_endpoint = _endpoint;
  cmd.manuf_specific = 0x00U;
  cmd.dis_default_resp = 0x00U;
  cmd.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;
  return reportClusterAttribute(&cmd);
}

bool LinkAnalog::setAnalogInputUnits(uint16_t bacnetUnit) {
  esp_zb_attribute_list_t *cluster =
    esp_zb_cluster_list_get_cluster(_cluster_list, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  if (cluster == nullptr) {
    log_e("Failed to get analog input cluster for the unit");
    return false;
  }

  // EngineeringUnits is optional, so whether the SDK's cluster already carries
  // it is not something to rely on: add it, and if it is there already, write
  // the value instead. The stack copies the value, so a local is fine.
  esp_err_t ret = esp_zb_analog_input_cluster_add_attr(cluster, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_ENGINEERING_UNITS_ID, (void *)&bacnetUnit);
  if (ret != ESP_OK) {
    ret = esp_zb_cluster_update_attr(cluster, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_ENGINEERING_UNITS_ID, (void *)&bacnetUnit);
  }
  if (ret != ESP_OK) {
    log_w("Failed to set the analog input unit: 0x%x: %s", ret, esp_err_to_name(ret));
    return false;
  }
  return true;
}
