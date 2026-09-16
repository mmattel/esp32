// One writable numeric setting: a code default, overridden by NVS, overridden
// by whatever the coordinator writes to an Analog Output cluster.
//
// The interval and the reporting deadband behave identically, so the resolve /
// clamp / persist / mirror-back logic lives here once.

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "Zigbee.h"

class ZbSetting {
public:
  // The endpoint reports ZB_MANUFACTURER / ZB_MODEL like every other endpoint
  // of this device; the description is what tells the two settings apart on the
  // coordinator.
  ZbSetting(uint8_t endpoint, const char *nvsKey, const char *description, float defaultValue, float minValue,
            float maxValue, float step, uint32_t applicationType);

  // Resolves the effective value: code default unless NVS holds one.
  void load(Preferences &prefs);

  // Builds the endpoint and registers it. cb has to forward to note(), which
  // is the only part of this class that may run outside the main task.
  void addEndpoint(void (*cb)(float));

  // Called from the Zigbee task on an attribute write: records only.
  void note(float value) {
    _pending = value;
    _hasPending = true;
  }

  // Applies a recorded write from the main task: clamps to range and step,
  // persists it, and mirrors the effective value back to the coordinator.
  // Returns true when the value actually changed.
  bool applyPending(Preferences &prefs);

  // Pushes the effective value to the coordinator.
  void publish();

  float value() const {
    return _value;
  }

private:
  float sanitise(float raw) const;

  // Decimals to print the effective value with. A value is always a multiple of
  // the step, so anything finer than the step is a digit that cannot differ:
  // a 1 s interval prints as 30, a 0.1 °C deadband as 0.2. A step finer than
  // 0.1 would want more digits than this gives; neither setting has one.
  int decimals() const {
    return _step >= 1.0f ? 0 : 1;
  }

  ZigbeeAnalog _ep;
  const char *_nvsKey;
  const char *_description;
  float _default;
  // Not _min / _max: the ESP32 core's Arduino.h defines those as macros.
  float _minValue;
  float _maxValue;
  float _step;
  uint32_t _appType;
  float _value;

  volatile float _pending = 0;
  volatile bool _hasPending = false;
};
