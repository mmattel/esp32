// A Zigbee temperature endpoint that also publishes which physical sensor it
// reads.
//
// The obvious place for that would be the model identifier, but a coordinator
// treats the model identifier as the product type, not as an instance: putting
// a ROM code in it makes every board, and every sensor swap, look like a
// different product. The sensor id therefore goes into the Basic cluster's
// LocationDescription attribute (0x0010), which is exactly the "which thing is
// this endpoint about" field, while the model stays constant.

#pragma once

#include <Arduino.h>
#include "Zigbee.h"

class TempEndpoint : public ZigbeeTempSensor {
public:
  explicit TempEndpoint(uint8_t endpoint) : ZigbeeTempSensor(endpoint) {}

  // Call once before Zigbee.begin() to create the attribute; later calls
  // rewrite its value, which is what a slot picking up a different sensor
  // needs. Ids longer than 16 characters are rejected.
  bool setSensorId(const char *id);

private:
  bool _created = false;
};
