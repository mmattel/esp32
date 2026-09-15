// A Zigbee analog input endpoint for one link measurement, with a unit.
//
// ZigbeeAnalog covers everything an LQI needs - it is a bare 0..255 count - but
// an RSSI is dBm, and the ZCL analog input cluster carries its unit in the
// optional EngineeringUnits attribute (0x0075) as a BACnet unit number. The
// library has no setter for it, and coordinators use it to label the value:
// Zigbee2MQTT falls back to EngineeringUnits whenever the application type
// implies no unit of its own, which is the case here. So this adds it, the same
// way TempEndpoint adds LocationDescription.

#pragma once

#include <Arduino.h>
#include "Zigbee.h"

// The BACnet engineering unit number for dBm, which is what EngineeringUnits
// holds. The full list is in the BACnet spec, and the subset a coordinator
// understands is in Zigbee2MQTT's generateDefinition.ts (199 is plain dB).
static constexpr uint16_t BACNET_UNIT_DBM = 200;

class LinkAnalog : public ZigbeeAnalog {
public:
  explicit LinkAnalog(uint8_t endpoint) : ZigbeeAnalog(endpoint) {}

  // Call after addAnalogInput() and before Zigbee.begin(). Optional: without it
  // the value is simply unitless, so a failure is a warning, not fatal.
  bool setAnalogInputUnits(uint16_t bacnetUnit);
};
