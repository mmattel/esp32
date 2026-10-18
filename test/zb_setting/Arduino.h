#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
using std::isnan;
using std::isinf;
#include <string>
typedef std::string String;
#define NVS_STUB 1
// The task running now. ZbSetting tells a mirror-back (the main task, inside
// publish()) from a coordinator write (the Zigbee task) by it; the Zigbee stub's
// injectWrite() switches to the Zigbee task for the length of its callback.
typedef void *TaskHandle_t;
inline TaskHandle_t const STUB_MAIN_TASK = (TaskHandle_t)1;
inline TaskHandle_t const STUB_ZIGBEE_TASK = (TaskHandle_t)2;
inline TaskHandle_t stubCurrentTask = STUB_MAIN_TASK;
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return stubCurrentTask; }
struct SerialStub {
  template <typename... A> void printf(const char *f, A... a) { std::printf(f, a...); }
  void println(const char *s = "") { std::printf("%s\n", s); }
};
static SerialStub Serial;
