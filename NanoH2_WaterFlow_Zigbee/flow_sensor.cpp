#include "flow_sensor.h"
#include "config.h"

// Shared between the ISR and the main task.  portMUX gives the ISR a
// way to protect its increment without disabling all interrupts.
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_pulseCount = 0;

void IRAM_ATTR flowSensorISR() {
  portENTER_CRITICAL_ISR(&s_mux);
  s_pulseCount++;
  portEXIT_CRITICAL_ISR(&s_mux);
}

FlowSensor::FlowSensor(uint8_t pin) : _pin(pin) {}

void FlowSensor::begin() {
  // INPUT_PULLUP keeps the line defined when no LLC is attached or when
  // the sensor side is disconnected; it does not interfere with an LLC
  // that already provides its own pull-up.
  pinMode(_pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(_pin), flowSensorISR, FLOW_PULSE_EDGE);
}

uint32_t FlowSensor::takePulses() {
  portENTER_CRITICAL(&s_mux);
  uint32_t count = s_pulseCount;
  s_pulseCount = 0;
  portEXIT_CRITICAL(&s_mux);
  return count;
}
