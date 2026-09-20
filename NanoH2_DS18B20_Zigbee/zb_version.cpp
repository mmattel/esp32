#include "zb_version.h"
#include "config.h"

// Same guard as zb_mirror.h, for the same reason: a config.h from before this
// endpoint existed has neither attribute number, and said once here it is one line
// naming the file to update rather than an error per use.
#if !defined(VERSION_TEXT_ATTR_ID) || !defined(VERSION_SW_BUILD_ID_ATTR)
#error "config.h has no 'Firmware version endpoint' section - update the whole sketch folder from one commit"
#endif

// Builds a ZCL character string: a leading length byte, then the characters. Not
// padded - see VERSION_TEXT_MAX - and the terminator is only there because the
// buffer is also printed in the failure message below.
//
// Refuses a string that does not fit rather than cutting it, because a cut version
// reads as a different version, which is worse than no version at all: "2.0" is a
// perfectly plausible answer and a wrong one.
static bool zclString(char *out, size_t room, const char *text) {
  size_t len = strlen(text);
  if (len + 2 > room) {
    return false;
  }
  out[0] = (char)len;
  memcpy(out + 1, text, len);
  out[len + 1] = '\0';
  return true;
}

bool SwBuildAnalog::addSoftwareBuildId(const char *version) {
  esp_zb_attribute_list_t *cluster =
    esp_zb_cluster_list_get_cluster(_cluster_list, ESP_ZB_ZCL_CLUSTER_ID_BASIC, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  if (cluster == nullptr) {
    log_e("Failed to get basic cluster for the firmware version");
    return false;
  }

  char zcl[VERSION_TEXT_MAX + 2];
  if (!zclString(zcl, sizeof(zcl), version)) {
    log_e("Firmware version '%s' is longer than %u characters", version, (unsigned)VERSION_TEXT_MAX);
    return false;
  }

  // The generic add, not esp_zb_basic_cluster_add_attr() as TempEndpoint uses for
  // LocationDescription: that helper only knows the attributes it was written for,
  // and SWBuildID is optional enough that whether this SDK generation is one of
  // them is not worth depending on. The generic call states the type and the access
  // itself, which is also what makes the answer read-only here - the same call
  // ZbMirror::addText() makes for its line. The stack copies the value, so a local
  // buffer is fine.
  esp_err_t ret = esp_zb_cluster_add_attr(cluster, ESP_ZB_ZCL_CLUSTER_ID_BASIC, VERSION_SW_BUILD_ID_ATTR,
                                          ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY,
                                          (void *)zcl);
  if (ret != ESP_OK) {
    // An SDK that does create SWBuildID with the rest of the Basic cluster refuses
    // to add it twice, and then the value is what has to be set instead. Either way
    // the attribute ends up holding this build's version.
    ret = esp_zb_cluster_update_attr(cluster, VERSION_SW_BUILD_ID_ATTR, (void *)zcl);
  }
  if (ret != ESP_OK) {
    log_w("Failed to set the firmware version on endpoint %u: 0x%x: %s", _endpoint, ret, esp_err_to_name(ret));
    return false;
  }
  return true;
}

bool ZbVersion::addText(const char *version) {
  esp_zb_attribute_list_t *cluster =
    esp_zb_cluster_list_get_cluster(_cluster_list, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  if (cluster == nullptr) {
    log_e("Failed to get analog input cluster for the firmware version");
    return false;
  }

  char zcl[VERSION_TEXT_MAX + 2];
  if (!zclString(zcl, sizeof(zcl), version)) {
    log_e("Firmware version '%s' is longer than %u characters", version, (unsigned)VERSION_TEXT_MAX);
    return false;
  }

  // Created with its final value, at its own length: the version is fixed at
  // compile time, so there is no later write for the length to be too short for -
  // which is what the mirror's full-length padding is for and this does not need.
  // Reportable all the same, so that publish() can send it after a join instead of
  // leaving the expose empty until somebody presses read.
  esp_err_t ret = esp_zb_cluster_add_attr(cluster, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT, VERSION_TEXT_ATTR_ID,
                                          ESP_ZB_ZCL_ATTR_TYPE_CHAR_STRING,
                                          ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
                                          (void *)zcl);
  if (ret != ESP_OK) {
    log_w("Failed to add the firmware version attribute: 0x%x: %s", ret, esp_err_to_name(ret));
    return false;
  }
  _hasText = true;
  return true;
}

bool ZbVersion::reportText() {
  // Built field by field rather than through a library helper, for the reasons
  // ZbMirror::reportText() sets out at length: the library reports the attributes it
  // knows about and not one added by hand, and manuf_code has to say which
  // manufacturer's 0xF000 this is - no manufacturer's, since the attribute is added
  // with the non-manufacturer call. Zeroed first so nothing is left to whatever the
  // stack happened to hold.
  //
  // Not shared with the mirror's copy: reportClusterAttribute() is protected, so a
  // common helper would have to be a common base class, and the two endpoints have
  // nothing else in common.
  esp_zb_zcl_report_attr_cmd_t cmd = {};
  cmd.address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT;
  cmd.attributeID = VERSION_TEXT_ATTR_ID;
  cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
  cmd.clusterID = ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT;
  cmd.zcl_basic_cmd.src_endpoint = _endpoint;
  cmd.manuf_specific = 0x00U;
  cmd.dis_default_resp = 0x00U;
  cmd.manuf_code = ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC;

  return reportClusterAttribute(&cmd);
}

void ZbVersion::publish(int32_t versionNumber) {
  if (!Zigbee.connected()) {
    return;  // nobody to tell, and nothing here that has to be caught up later
  }

  // The text first and the number second, as the mirror does it: a coordinator
  // acting on the number already has the string it belongs to.
  if (_hasText) {
    reportText();
  }
  setAnalogInput((float)versionNumber);
  reportAnalogInput();
}
