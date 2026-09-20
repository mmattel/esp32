#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
using std::isnan;
using std::snprintf;
// The real Arduino.h reaches sys/param.h through FreeRTOS, which brings the POSIX
// limit macros with it, so a constant in the sketch that happens to share a name
// with one of them expands to a number before the compiler sees the declaration.
// LINE_MAX did, once. Including it here means the next one fails on this machine
// rather than on the board.
#include <climits>

// The clock the test drives, so the age of a reading can be checked without
// waiting the hour out. Defined in test.cpp.
extern uint32_t hostMillis;
static inline uint32_t millis() {
  return hostMillis;
}

// Everything printed is kept as well as shown: half of what slots.cpp decides is
// which lines stay on the console and which also go on the air, and the tests can
// only see that difference if both halves are readable. logEvent() is captured
// separately, in test.cpp.
extern void consoleCapture(const char *text);

struct SerialStub {
  template <typename... A> void printf(const char *f, A... a) {
    char line[256];
    std::snprintf(line, sizeof(line), f, a...);
    consoleCapture(line);
    std::printf("%s", line);
  }
  void println(const char *s = "") {
    consoleCapture(s);
    consoleCapture("\n");
    std::printf("%s\n", s);
  }
};
static SerialStub Serial;
