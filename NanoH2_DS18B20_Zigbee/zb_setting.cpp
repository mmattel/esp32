#include "zb_setting.h"
#include "config.h"

ZbSetting::ZbSetting(uint8_t endpoint, const char *nvsKey, const char *description, float defaultValue,
                     float minValue, float maxValue, float step, uint32_t applicationType)
  : _ep(endpoint), _nvsKey(nvsKey), _description(description), _default(defaultValue), _minValue(minValue),
    _maxValue(maxValue), _step(step), _appType(applicationType), _value(defaultValue) {}

float ZbSetting::sanitise(float raw) const {
  if (isnan(raw)) {
    return _default;
  }
  float value = raw;
  if (_step > 0) {
    value = roundf(value / _step) * _step;
  }
  if (value < _minValue) {
    value = _minValue;
  }
  if (value > _maxValue) {
    value = _maxValue;
  }
  return value;
}

void ZbSetting::load(Preferences &prefs) {
  // getFloat() stores and reads a blob, so a key written with a different type
  // fails cleanly and leaves the NAN default in place.
  float stored = prefs.getFloat(_nvsKey, NAN);
  bool fromNvs = !isnan(stored);
  _value = fromNvs ? sanitise(stored) : _default;
  Serial.printf("%s: %.*f (%s)\r\n", _description, decimals(), _value, fromNvs ? "from NVS" : "code default");
}

void ZbSetting::addEndpoint(void (*cb)(float)) {
  _ep.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL);
  _ep.addAnalogOutput();
  _ep.setAnalogOutputApplication(_appType);
  _ep.setAnalogOutputDescription(_description);
  _ep.setAnalogOutputResolution(_step);
  _ep.setAnalogOutputMinMax(_minValue, _maxValue);
  _ep.onAnalogOutputChange(cb);
  _ep.setPowerSource(ZB_POWER_SOURCE_MAINS);
  Zigbee.addEndpoint(&_ep);
}

bool ZbSetting::applyPending(Preferences &prefs) {
  if (!_hasPending) {
    return false;
  }
  _hasPending = false;

  float requested = _pending;
  float applied = sanitise(requested);
  bool changed = fabsf(applied - _value) > (_step > 0 ? _step / 2 : 1e-6f);
  // The write did not survive as sent: rounding to the step or clamping to the
  // range moved it. The attribute in the stack still holds what was written, so
  // this is what decides whether it has to be corrected.
  bool corrected = applied != requested;

  // Only a write that moved something is worth a line: one that moves the value,
  // or one this took a liberty with. A coordinator repeating the value already in
  // effect changes nothing anywhere and says nothing.
  // The request keeps two decimals whatever the step is: it is what the
  // coordinator asked for, and showing it unrounded is what makes a rounded or
  // clamped write visible as one.
  if (changed || corrected) {
    Serial.printf("%s written from Zigbee: %.2f -> %.*f\r\n", _description, requested, decimals(), applied);
  }

  _value = applied;
  if (changed) {
    prefs.putFloat(_nvsKey, _value);
  }
  // A write the stack accepted verbatim needs no mirror-back: the attribute
  // already holds it. Mirroring anyway would hand the coordinator our float of
  // the same number, which is the same value but rarely the same digits.
  if (corrected) {
    publish();
  }
  return changed;
}

void ZbSetting::publish() {
  // setAnalogOutput() runs the change callback before it touches the attribute,
  // so this is where note() has to know the value is our own - see note().
  _mirroring = true;
  _ep.setAnalogOutput(_value);
  _mirroring = false;
  _ep.reportAnalogOutput();
}
