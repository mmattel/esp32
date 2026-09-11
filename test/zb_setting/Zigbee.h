#pragma once
#include <Arduino.h>
enum zb_power_source_t { ZB_POWER_SOURCE_MAINS = 1 };
class ZigbeeEP { public: virtual ~ZigbeeEP() {} };
class ZigbeeAnalog : public ZigbeeEP {
public:
  float output = NAN;
  int reports = 0;
  explicit ZigbeeAnalog(uint8_t) {}
  bool setManufacturerAndModel(const char *, const char *) { return true; }
  bool addAnalogOutput() { return true; }
  bool setAnalogOutputApplication(uint32_t) { return true; }
  bool setAnalogOutputDescription(const char *) { return true; }
  bool setAnalogOutputResolution(float) { return true; }
  bool setAnalogOutputMinMax(float, float) { return true; }
  void onAnalogOutputChange(void (*)(float)) {}
  bool setPowerSource(zb_power_source_t, uint8_t = 0xff, uint8_t = 0xff) { return true; }
  bool setAnalogOutput(float v) { output = v; return true; }
  bool reportAnalogOutput() { reports++; return true; }
};
struct ZigbeeCoreStub { bool addEndpoint(ZigbeeEP *) { return true; } };
static ZigbeeCoreStub Zigbee;
#define ESP_ZB_ZCL_AI_TIME_RELATIVE 0u
#define ESP_ZB_ZCL_AI_TEMPERATURE_OTHER 1u
