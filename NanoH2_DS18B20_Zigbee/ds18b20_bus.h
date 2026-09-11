// Minimal 1-Wire master plus DS18B20 support, bit-banged on a single GPIO.
//
// This exists instead of the usual OneWire/DallasTemperature pair because
// OneWire's direct-GPIO layer (util/OneWire_direct_gpio.h) only special-cases
// ESP32-C3 and C6; on the ESP32-H2 it falls through to the "plain ESP32"
// branch and references GPIO.in1 / GPIO.out1_w1ts, which do not exist on this
// SoC. Rather than patch a library, the protocol is implemented here.
//
// Wiring: external power (not parasite), one 4.7 kOhm pull-up from the data
// line to the sensor supply rail.

#pragma once

#include <Arduino.h>

struct DS18B20Reading {
  bool valid;
  float celsius;
};

class DS18B20Bus {
public:
  explicit DS18B20Bus(uint8_t pin) : _pin(pin) {}

  // Configures the pin as open-drain with input enabled, so the line can be
  // driven low and released without further pinMode() calls.
  void begin();

  // Enumerates DS18B20 devices (family code 0x28) on the bus. ROM codes are
  // packed little-endian: byte 0 (the family code) occupies the low 8 bits.
  // Returns the number of ROM codes written to roms.
  uint8_t discover(uint64_t *roms, uint8_t maxRoms);

  // Programs 12-bit resolution into the scratchpad (not EEPROM, so it is
  // re-applied on every boot and the sensor's EEPROM is left alone).
  bool setResolution12bit(uint64_t rom);

  // Broadcasts CONVERT T to every device on the bus at once.
  bool startConversionAll();

  // Conversion time for 12-bit resolution.
  static uint32_t conversionTimeMs() {
    return 750;
  }

  // Reads the scratchpad of one device and converts it. Returns valid=false
  // on a CRC error, a missing device or an out-of-range value.
  DS18B20Reading read(uint64_t rom);

  // 16 uppercase hex digits, family code first: "28FF641E1234ABCD".
  static String romToString(uint64_t rom);

private:
  static const uint8_t CMD_SEARCH_ROM = 0xF0;
  static const uint8_t CMD_MATCH_ROM = 0x55;
  static const uint8_t CMD_SKIP_ROM = 0xCC;
  static const uint8_t CMD_CONVERT_T = 0x44;
  static const uint8_t CMD_WRITE_SCRATCHPAD = 0x4E;
  static const uint8_t CMD_READ_SCRATCHPAD = 0xBE;
  static const uint8_t FAMILY_DS18B20 = 0x28;

  bool reset();
  void writeBit(uint8_t bit);
  uint8_t readBit();
  void writeByte(uint8_t value);
  uint8_t readByte();
  void writeRom(uint64_t rom);
  bool searchNext(uint8_t *rom);
  void searchReset();
  static uint8_t crc8(const uint8_t *data, uint8_t len);

  uint8_t _pin;

  // Maxim ROM search state.
  uint8_t _searchRom[8] = {0};
  uint8_t _lastDiscrepancy = 0;
  bool _lastDeviceFlag = false;
};
