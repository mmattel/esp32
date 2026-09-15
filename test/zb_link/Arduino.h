#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
// The real esp32-hal-log.h macro; here it just prints.
#define log_w(fmt, ...) std::printf(fmt "\n", ##__VA_ARGS__)
typedef uint32_t TickType_t;
#define portMAX_DELAY 0xffffffffu
