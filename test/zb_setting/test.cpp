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

// The core takes a bare function pointer with no context, so the sketch gives
// each setting a trampoline; these tests need one too.
static ZbSetting *target = nullptr;
static void onWritten(float value) {
  if (target) target->note(value);
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
  printf("interval sanitise (default 30, 10..3600 step 1)\n");
  ZbSetting iv = makeInterval();
  check("below min -> min",        iv.sanitise(5),      TEMP_INTERVAL_MIN_S);
  check("above max -> max",        iv.sanitise(99999),  TEMP_INTERVAL_MAX_S);
  check("fractional -> rounded",   iv.sanitise(42.6f),  43);
  check("NaN -> code default",     iv.sanitise(NAN),    TEMP_INTERVAL_DEFAULT_S);
  check("in range untouched",      iv.sanitise(300),    300);

  printf("delta sanitise (default 0.25, 0..20 step 0.25)\n");
  ZbSetting dl = makeDelta();
  check("negative -> 0",           dl.sanitise(-3),     0.0f);
  check("0.26 -> 0.25",            dl.sanitise(0.26f),  0.25f);
  check("0.10 -> 0.0",             dl.sanitise(0.10f),  0.0f);
  check("above max -> max",        dl.sanitise(100),    TEMP_DELTA_MAX_C);
  check("NaN -> code default",     dl.sanitise(NAN),    TEMP_DELTA_DEFAULT_C);
  // Why the step is a quarter: every multiple of it is exact in a float, so the
  // value a coordinator sets is bit for bit the value it reads back. A tenth is
  // not, which is how 0.7 became 0.700000010430813 on the coordinator.
  printf("  quarters survive the float exactly -> %s\n",
         dl.sanitise(0.75f) == 0.75f && dl.sanitise(19.25f) == 19.25f ? "yes (ok)" : "no (FAIL)");
  if (!(dl.sanitise(0.75f) == 0.75f && dl.sanitise(19.25f) == 19.25f)) fails++;

  printf("printed decimals follow the step\n");
  check("1 s step -> whole seconds",  iv.decimals(), 0);
  check("0.25 C step -> two decimals", dl.decimals(), 2);

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
  // Taken as sent, so the attribute the stack already holds is left alone - see
  // the mirror-back block below.
  printf("  taken as sent -> attribute left alone -> %s\n", isnan(d._ep.output) ? "yes (ok)" : "no (FAIL)");
  if (!isnan(d._ep.output)) fails++;

  d.note(2.5f);
  changed = d.applyPending(q);
  printf("  rewriting same value -> changed=%s\n", changed ? "true (FAIL)" : "false (ok)");
  if (changed) fails++;

  d.note(50.0f);
  d.applyPending(q);
  check("clamped write persisted", q.getFloat(NVS_KEY_DELTA, NAN), TEMP_DELTA_MAX_C);
  check("clamped write mirrored",  d._ep.output, TEMP_DELTA_MAX_C);

  // A write taken verbatim leaves the attribute alone: the stack already holds
  // it, and mirroring would only replace the coordinator's digits with ours.
  printf("mirror-back only for a write that was not taken as sent\n");
  ZbSetting e = makeDelta();
  Preferences r;
  e.load(r);
  e.addEndpoint(onWritten);  // as the sketch does, so the core's setter can call back
  target = &e;
  int reportsBefore = e._ep.reports;
  e.note(1.5f);  // on the step, in range
  e.applyPending(r);
  printf("  verbatim write touched the attribute -> %s\n", isnan(e._ep.output) ? "no (ok)" : "yes (FAIL)");
  if (!isnan(e._ep.output)) fails++;
  printf("  verbatim write reported -> %s\n", e._ep.reports == reportsBefore ? "no (ok)" : "yes (FAIL)");
  if (e._ep.reports != reportsBefore) fails++;
  e.note(1.53f);  // off the step, so it is rounded and has to be corrected
  e.applyPending(r);
  check("rounded write mirrored", e._ep.output, 1.5f);

  // The core's setter runs the change callback, so publishing used to look like a
  // write from the coordinator: applied, mirrored, noted, applied ... every loop.
  printf("publishing does not queue a write\n");
  e.publish();
  printf("  publish() left a pending write -> %s\n", e._hasPending ? "yes (FAIL)" : "no (ok)");
  if (e._hasPending) fails++;
  reportsBefore = e._ep.reports;
  for (int i = 0; i < 5; i++) {
    e.applyPending(r);
  }
  printf("  five idle loops -> %d report(s) %s\n", e._ep.reports - reportsBefore,
         e._ep.reports == reportsBefore ? "(ok)" : "(FAIL)");
  if (e._ep.reports != reportsBefore) fails++;

  // A real write still gets through after all that.
  target = &e;
  onWritten(3.0f);
  changed = e.applyPending(r);
  printf("  real write after a publish -> changed=%s\n", changed ? "true (ok)" : "false (FAIL)");
  if (!changed) fails++;

  printf("\n%s\n", fails ? "FAILURES" : "ALL PASS");
  return fails != 0;
}
