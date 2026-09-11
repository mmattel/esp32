#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#define LOW 0
#define HIGH 1
#define OUTPUT_OPEN_DRAIN 0x13
#define PULLUP 0x04
typedef std::string String;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
inline void portENTER_CRITICAL(portMUX_TYPE*) {}
inline void portEXIT_CRITICAL(portMUX_TYPE*) {}
inline void pinMode(uint8_t, uint32_t) {}
// --- simulated 1-Wire bus, defined in test.cpp ---
void sim_write(uint8_t pin, uint8_t level);
int  sim_read(uint8_t pin);
void sim_delay(uint32_t us);
#define digitalWrite(p, v) sim_write((p), (v))
#define digitalRead(p) sim_read((p))
#define delayMicroseconds(us) sim_delay((us))
