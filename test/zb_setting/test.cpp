// Host tests for ZbSetting: the code-default / NVS / Zigbee precedence and the
// rounding, clamping, persistence and mirror-back of a written value.
//
// Arduino.h, Preferences.h and Zigbee.h in this directory are stubs. The
// Preferences stub mirrors the real float behaviour (stored as a blob, a miss
// leaves the caller's NAN default in place), which is what the NAN sentinel in
// ZbSetting::load() relies on.

#include "Arduino.h"
#define private public
#include "../../NanoH2_DS18B20_Zigbee/zb_setting.cpp"
#undef private
#include <cassert>

static int fails = 0;
static void check(const char *what, float got, float want) {
  bool ok = fabsf(got - want) < 1e-4f;
  printf("  %-38s got %8.3f  want %8.3f  %s\n", what, got, want, ok ? "ok" : "FAIL");
  if (!ok) fails++;
}

ZbSetting makeInterval() {
  return ZbSetting(13, NVS_KEY_INTERVAL, "Reading interval (s)", TEMP_INTERVAL_DEFAULT_S, TEMP_INTERVAL_MIN_S,
                   TEMP_INTERVAL_MAX_S, TEMP_INTERVAL_STEP_S, ESP_ZB_ZCL_AI_TIME_RELATIVE);
}
ZbSetting makeDelta() {
  return ZbSetting(14, NVS_KEY_DELTA, "Reporting delta (C)", TEMP_DELTA_DEFAULT_C, TEMP_DELTA_MIN_C,
                   TEMP_DELTA_MAX_C, TEMP_DELTA_STEP_C, ESP_ZB_ZCL_AI_TEMPERATURE_OTHER);
}

int main() {
  printf("interval sanitise (default 60, 10..3600 step 1)\n");
  ZbSetting iv = makeInterval();
  check("below min -> min",        iv.sanitise(5),      TEMP_INTERVAL_MIN_S);
  check("above max -> max",        iv.sanitise(99999),  TEMP_INTERVAL_MAX_S);
  check("fractional -> rounded",   iv.sanitise(42.6f),  43);
  check("NaN -> code default",     iv.sanitise(NAN),    TEMP_INTERVAL_DEFAULT_S);
  check("in range untouched",      iv.sanitise(300),    300);

  printf("delta sanitise (default 0.5, 0..20 step 0.1)\n");
  ZbSetting dl = makeDelta();
  check("negative -> 0",           dl.sanitise(-3),     0.0f);
  check("0.26 -> 0.3",             dl.sanitise(0.26f),  0.3f);
  check("0.04 -> 0.0",            dl.sanitise(0.04f),  0.0f);
  check("above max -> max",        dl.sanitise(100),    TEMP_DELTA_MAX_C);
  check("NaN -> code default",     dl.sanitise(NAN),    TEMP_DELTA_DEFAULT_C);

  printf("load() precedence\n");
  Preferences p;
  ZbSetting a = makeDelta();
  a.load(p);
  check("no NVS key -> code default", a.value(), TEMP_DELTA_DEFAULT_C);
  p.putFloat(NVS_KEY_DELTA, 1.5f);
  ZbSetting b = makeDelta();
  b.load(p);
  check("NVS wins over default", b.value(), 1.5f);
  p.putFloat(NVS_KEY_DELTA, 999.0f);
  ZbSetting c = makeDelta();
  c.load(p);
  check("NVS value still clamped", c.value(), TEMP_DELTA_MAX_C);

  printf("applyPending()\n");
  Preferences q;
  ZbSetting d = makeDelta();
  d.load(q);
  printf("  no pending write -> %s\n", d.applyPending(q) ? "changed (FAIL)" : "no change (ok)");
  if (d.applyPending(q)) fails++;

  d.note(2.5f);
  bool changed = d.applyPending(q);
  printf("  write 2.5 -> changed=%s\n", changed ? "true (ok)" : "false (FAIL)");
  if (!changed) fails++;
  check("value applied", d.value(), 2.5f);
  check("persisted to NVS", q.getFloat(NVS_KEY_DELTA, NAN), 2.5f);
  check("mirrored to coordinator", d._ep.output, 2.5f);

  d.note(2.5f);
  changed = d.applyPending(q);
  printf("  rewriting same value -> changed=%s\n", changed ? "true (FAIL)" : "false (ok)");
  if (changed) fails++;

  d.note(50.0f);
  d.applyPending(q);
  check("clamped write persisted", q.getFloat(NVS_KEY_DELTA, NAN), TEMP_DELTA_MAX_C);
  check("clamped write mirrored",  d._ep.output, TEMP_DELTA_MAX_C);

  printf("\n%s\n", fails ? "FAILURES" : "ALL PASS");
  return fails != 0;
}
