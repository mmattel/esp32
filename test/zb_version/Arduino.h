#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

// The real esp32-hal-log.h macros; here they just print.
#define log_e(fmt, ...) std::printf(fmt "\n", ##__VA_ARGS__)
#define log_w(fmt, ...) std::printf(fmt "\n", ##__VA_ARGS__)

// Nothing in zb_version.cpp reads the clock - the version cannot change while the
// device runs, so there is no interval, no deadband and no heartbeat to drive.

struct SerialStub {
  template <typename... A> void printf(const char *f, A... a) { std::printf(f, a...); }
  void println(const char *s = "") { std::printf("%s\n", s); }
};
static SerialStub Serial;
