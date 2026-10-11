#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
// The real esp32-hal-log.h macro; here it just prints.
#define log_w(fmt, ...) std::printf(fmt "\n", ##__VA_ARGS__)
typedef uint32_t TickType_t;

// The clock the test drives, so the time a probe's answer took can be checked.
extern uint32_t hostMillis;
static inline uint32_t millis() {
  return hostMillis;
}
#define portMAX_DELAY 0xffffffffu
