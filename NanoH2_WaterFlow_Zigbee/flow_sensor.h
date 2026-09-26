// Hall-effect water flow sensor - pulse counting.
//
// The sensor generates one pulse per paddle-wheel revolution.  The number
// of pulses per litre is the calibration constant configured in config.h
// (FLOW_IMPULSES_PER_L_DEFAULT) and adjustable from the coordinator.
//
// The ISR counts every falling edge on PIN_FLOW into a shared counter.
// takePulses() reads and resets that counter atomically; the caller is
// responsible for the interval timing and the unit conversion.

#pragma once

#include <Arduino.h>

class FlowSensor {
public:
  explicit FlowSensor(uint8_t pin);

  // Sets the pin mode and attaches the interrupt.  Call once in setup().
  void begin();

  // Atomically reads and clears the pulse counter.  Returns the number of
  // pulses since the last call (or since begin()).
  uint32_t takePulses();

private:
  uint8_t _pin;
};
