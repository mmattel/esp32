#include "zb_link_endpoint.h"

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
