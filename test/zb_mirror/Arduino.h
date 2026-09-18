#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdarg>
// The real Arduino.h reaches sys/param.h through FreeRTOS, which brings the POSIX
// limit macros with it, so a constant in the sketch that happens to share a name
// with one of them expands to a number before the compiler sees the declaration.
// LINE_MAX did, once. Including it here means the next one fails on this machine
// rather than on the board.
#include <climits>

// The real esp32-hal-log.h macros; here they just print.
#define log_e(fmt, ...) std::printf(fmt "\n", ##__VA_ARGS__)
#define log_w(fmt, ...) std::printf(fmt "\n", ##__VA_ARGS__)

// The clock the test drives, so the heartbeat can be reached without waiting the
// hour out. Defined in test.cpp.
extern uint32_t hostMillis;
static inline uint32_t millis() {
  return hostMillis;
}

struct SerialStub {
  template <typename... A> void printf(const char *f, A... a) { std::printf(f, a...); }
  void println(const char *s = "") { std::printf("%s\n", s); }
};
static SerialStub Serial;
