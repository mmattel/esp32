#pragma once
#include <Arduino.h>
enum zb_power_source_t { ZB_POWER_SOURCE_MAINS = 1 };
class ZigbeeEP { public: virtual ~ZigbeeEP() {} };
class ZigbeeAnalog : public ZigbeeEP {
public:
  float output = NAN;
  int reports = 0;  // counted by the test's zbReport(), found through byEndpoint
  explicit ZigbeeAnalog(uint8_t endpoint) : _endpoint(endpoint) { byEndpoint[endpoint] = this; }
  uint8_t getEndpoint() { return _endpoint; }
  // The endpoint each value was last constructed on, so zbReport(), which is only
  // given a number, can credit the report to the object the test holds.
  static inline ZigbeeAnalog *byEndpoint[256] = {};
  bool setManufacturerAndModel(const char *, const char *) { return true; }
  bool addAnalogOutput() { return true; }
  bool setAnalogOutputApplication(uint32_t) { return true; }
  bool setAnalogOutputDescription(const char *) { return true; }
  bool setAnalogOutputResolution(float) { return true; }
  bool setAnalogOutputMinMax(float, float) { return true; }
  void onAnalogOutputChange(void (*cb)(float)) { changed = cb; }
  bool setPowerSource(zb_power_source_t, uint8_t = 0xff, uint8_t = 0xff) { return true; }
  // Like the core's ZigbeeAnalog: the setter runs the change callback first and
  // only then stores the attribute, so a mirror-back arrives at the callback as
  // if the coordinator had written it.
  bool setAnalogOutput(float v) {
    if (changed) { changed(v); }
    output = v;
    return true;
  }
  // A write from the coordinator, in the order the stack does it: the attribute
  // holds the written value by the time the callback runs.
  void injectWrite(float v) {
    output = v;
    if (changed) { changed(v); }
  }
  void (*changed)(float) = nullptr;
  // No reportAnalogOutput(): the core's reports through the binding table, and the
  // sketch must not call it - see zb_report.h. Leaving it out makes a call that
  // creeps back in a compile error here.
private:
  uint8_t _endpoint;
};
struct ZigbeeCoreStub { bool addEndpoint(ZigbeeEP *) { return true; } };
static ZigbeeCoreStub Zigbee;
#define ESP_ZB_ZCL_CLUSTER_ID_ANALOG_OUTPUT 0x000d
#define ESP_ZB_ZCL_ATTR_ANALOG_OUTPUT_PRESENT_VALUE_ID 0x0055
#define ESP_ZB_ZCL_AI_TIME_RELATIVE 0u
#define ESP_ZB_ZCL_AI_TEMPERATURE_OTHER 1u
