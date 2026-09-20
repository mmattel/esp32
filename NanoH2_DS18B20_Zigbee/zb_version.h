// Which firmware is running, put where something other than a serial console can
// read it.
//
// Two ways, because they answer the question for different readers and cost
// different things:
//
//   SwBuildAnalog adds the version to the Basic cluster's SWBuildID attribute
//   (0x4000). That is where a coordinator already looks - Zigbee2MQTT shows it as
//   "Firmware build ID" on the device page - and it needs no converter, no binding
//   and no reporting, because it is read once during the interview. It is the half
//   that works on a device nobody has written a converter for, and it is why this
//   is a class of its own: ZbSetting's endpoints get it too, so the attribute is
//   there whichever endpoint a coordinator decides to read Basic from.
//
//   ZbVersion is the endpoint: the version as a number in an Analog Input cluster,
//   and as a string in a text attribute beside it, the same construction the
//   console mirror uses for its line. The number is what an automation can put a
//   condition on or a dashboard can graph; a string can only be looked at.
//
// Nothing here changes while the device runs. A version can only change by
// flashing, and flashing reboots, so both values are created once in setup() and
// publish() simply puts them on the air again after each join.

#pragma once

#include <Arduino.h>
#include "Zigbee.h"

// The longest version string that can go on the air, "255.255.255" plus room to
// spare. The attribute is created at the exact length of the string it is given,
// not padded to this: the version is fixed at compile time, so unlike the mirror's
// line there is never a longer value to make room for.
static constexpr size_t VERSION_TEXT_MAX = 16;

// Any analog endpoint that also says which firmware it belongs to. The Basic
// cluster is built by the core for every endpoint, but the core has no setter for
// SWBuildID and _cluster_list is protected, so reaching it takes a subclass - the
// same reason LinkAnalog and TempEndpoint are subclasses.
class SwBuildAnalog : public ZigbeeAnalog {
public:
  explicit SwBuildAnalog(uint8_t endpoint) : ZigbeeAnalog(endpoint) {}

  // Call before Zigbee.begin(), while the attribute list is still ours. Optional:
  // without it the endpoint simply does not state its firmware version, so a
  // failure is a warning, not fatal.
  bool addSoftwareBuildId(const char *version);
};

class ZbVersion : public SwBuildAnalog {
public:
  explicit ZbVersion(uint8_t endpoint) : SwBuildAnalog(endpoint) {}

  // Call after addAnalogInput() and before Zigbee.begin(). Creates the text
  // attribute and fills it in for good - there is nothing to update later.
  // Optional, like addText() on the mirror: without it the endpoint still carries
  // the version as a number.
  bool addText(const char *version);

  // Puts both on the air: the number as the cluster's value, and the text beside
  // it. Called on every join rather than on a timer, because that is the only
  // moment the answer can have changed - a new version means a flash, a flash
  // means a reboot, and a reboot means a join.
  void publish(int32_t versionNumber);

private:
  bool reportText();

  bool _hasText = false;  // whether addText() got the attribute created
};
