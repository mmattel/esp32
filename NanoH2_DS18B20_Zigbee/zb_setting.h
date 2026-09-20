// One writable numeric setting: a code default, overridden by NVS, overridden
// by whatever the coordinator writes to an Analog Output cluster.
//
// The interval and the reporting deadband behave identically, so the resolve /
// clamp / persist / mirror-back logic lives here once.

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "Zigbee.h"
#include "zb_version.h"

class ZbSetting {
public:
  // The endpoint reports ZB_MANUFACTURER / ZB_MODEL like every other endpoint
  // of this device; the description is what tells the settings apart on the
  // coordinator.
  ZbSetting(uint8_t endpoint, const char *nvsKey, const char *description, float defaultValue, float minValue,
            float maxValue, float step, uint32_t applicationType);

  // Resolves the effective value: code default unless NVS holds one.
  void load(Preferences &prefs);

  // Builds the endpoint and registers it. cb has to forward to note(), which
  // is the only part of this class that may run outside the main task.
  void addEndpoint(void (*cb)(float));

  // Called from the Zigbee task on an attribute write: records only.
  // Named for where it comes from, so the line below cannot be read as comparing
  // one variable with itself: written is the coordinator's, _value is ours.
  void note(float written) {
    // The core's ZigbeeAnalog::setAnalogOutput() runs this same callback, so a
    // mirror-back of our own value comes round looking like a write from the
    // coordinator. Applying it would mirror it again, which would note it again -
    // a write, a report and a console line every loop, forever.
    //
    // Told apart by the value rather than by a flag held across the publish: the
    // core runs this callback before it takes the Zigbee lock to store the
    // attribute, so a flag would still be set while the main task waits for that
    // lock - and a genuine write dispatched in that window would be dropped
    // without a trace. A mirror-back always carries the value already in effect,
    // and a coordinator writing that same value asks for nothing: applyPending()
    // would find nothing changed and nothing to correct.
    if (written == _value) {
      return;
    }
    _pending = written;
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

  // Decimals to print the effective value with: the fewest that can still spell
  // the step, since a value is always a multiple of it and anything finer is a
  // digit that cannot differ. A 1 s interval prints as 30, a 0.25 °C deadband as
  // 0.25, a 0.1 °C one as 0.2. Two is the finest step either setting has.
  int decimals() const {
    if (_step >= 1.0f) {
      return 0;
    }
    return fabsf(roundf(_step * 10.0f) / 10.0f - _step) < 1e-6f ? 1 : 2;
  }

  // A plain ZigbeeAnalog in every respect that matters here; the subclass is only
  // there to put FW_VERSION in the endpoint's Basic cluster - see addEndpoint().
  SwBuildAnalog _ep;
  const char *_nvsKey;
  const char *_description;
  float _default;
  // Not _min / _max: the ESP32 core's Arduino.h defines those as macros.
  float _minValue;
  float _maxValue;
  float _step;
  uint32_t _appType;
  // Written by the main task, read by the Zigbee task in note() to recognise a
  // mirror-back. A 32-bit aligned load and store, so a read never sees half a
  // value; volatile keeps the compiler from holding a stale one.
  volatile float _value;

  volatile float _pending = 0;
  volatile bool _hasPending = false;
};
