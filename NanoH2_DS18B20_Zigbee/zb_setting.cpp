#include "zb_setting.h"
#include "config.h"

ZbSetting::ZbSetting(uint8_t endpoint, const char *nvsKey, const char *model, const char *description,
                     float defaultValue, float minValue, float maxValue, float step, uint32_t applicationType)
  : _ep(endpoint), _nvsKey(nvsKey), _model(model), _description(description), _default(defaultValue),
    _min(minValue), _max(maxValue), _step(step), _appType(applicationType), _value(defaultValue) {}

float ZbSetting::sanitise(float raw) const {
  if (isnan(raw)) {
    return _default;
  }
  float value = raw;
  if (_step > 0) {
    value = roundf(value / _step) * _step;
  }
  if (value < _min) {
    value = _min;
  }
  if (value > _max) {
    value = _max;
  }
  return value;
}

void ZbSetting::load(Preferences &prefs) {
  // getFloat() stores and reads a blob, so a key written with a different type
  // fails cleanly and leaves the NAN default in place.
  float stored = prefs.getFloat(_nvsKey, NAN);
  bool fromNvs = !isnan(stored);
  _value = fromNvs ? sanitise(stored) : _default;
  Serial.printf("%s: %.2f (%s)\r\n", _description, _value, fromNvs ? "from NVS" : "code default");
}

void ZbSetting::addEndpoint(void (*cb)(float)) {
  _ep.setManufacturerAndModel(ZB_MANUFACTURER, _model);
  _ep.addAnalogOutput();
  _ep.setAnalogOutputApplication(_appType);
  _ep.setAnalogOutputDescription(_description);
  _ep.setAnalogOutputResolution(_step);
  _ep.setAnalogOutputMinMax(_min, _max);
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

  Serial.printf("%s written from Zigbee: %.2f -> %.2f%s\r\n", _description, requested, applied,
                changed ? "" : " (no change)");

  _value = applied;
  if (changed) {
    prefs.putFloat(_nvsKey, _value);
  }
  // Mirror the effective value back either way, so a clamped or rounded write
  // shows up on the coordinator instead of silently diverging.
  publish();
  return changed;
}

void ZbSetting::publish() {
  _ep.setAnalogOutput(_value);
  _ep.reportAnalogOutput();
}
