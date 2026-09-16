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
  printf("interval sanitise (default 60, 10..3600 step 1)\n");
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

  // A value stored by a build with a finer step - 0.1 was legal before the step
  // became a quarter - is re-rounded on load and the result stored, so NVS and
  // the value in use cannot disagree for good. 0.1 rounds to 0, which publishes
  // every reading that moves at all, which is why load() says so.
  printf("a stored value that no longer fits the step is re-stored\n");
  Preferences old_;
  old_.putFloat(NVS_KEY_DELTA, 0.1f);
  ZbSetting g = makeDelta();
  g.load(old_);
  check("re-rounded to the step", g.value(), 0.0f);
  check("NVS holds the re-rounded value", old_.getFloat(NVS_KEY_DELTA, NAN), 0.0f);
  int writesBefore = old_.writes;
  ZbSetting h = makeDelta();
  h.load(old_);  // second boot: the stored value fits now, so nothing is written
  printf("  value that fits -> NVS untouched -> %s\n", old_.writes == writesBefore ? "yes (ok)" : "no (FAIL)");
  if (old_.writes != writesBefore) fails++;

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
  // Taken as sent, so nothing is pushed to the attribute - this endpoint has had
  // nothing pushed to it at all, which is what NaN says here. The block further
  // down checks the same rule against an attribute the write really went through.
  printf("  taken as sent -> nothing pushed to the attribute -> %s\n", isnan(d._ep.output) ? "yes (ok)" : "no (FAIL)");
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
  // it, and mirroring would only replace the coordinator's digits with ours. The
  // writes below go in through the stub's injectWrite(), which stores the value in
  // the attribute before calling back exactly as the stack does - so the attribute
  // afterwards shows whether the value in use and the value the coordinator can
  // read still agree.
  printf("mirror-back only for a write that was not taken as sent\n");
  ZbSetting e = makeDelta();
  Preferences r;
  e.load(r);
  e.addEndpoint(onWritten);  // as the sketch does, so the core's setter can call back
  target = &e;
  int reportsBefore = e._ep.reports;
  e._ep.injectWrite(1.5f);  // on the step, in range
  e.applyPending(r);
  check("verbatim write left in the attribute", e._ep.output, 1.5f);
  check("attribute agrees with the value in use", e._ep.output, e.value());
  printf("  verbatim write reported -> %s\n", e._ep.reports == reportsBefore ? "no (ok)" : "yes (FAIL)");
  if (e._ep.reports != reportsBefore) fails++;
  e._ep.injectWrite(1.53f);  // off the step, so it is rounded and has to be corrected
  e.applyPending(r);
  check("rounded write corrected in the attribute", e._ep.output, 1.5f);
  check("attribute agrees after a correction", e._ep.output, e.value());

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
