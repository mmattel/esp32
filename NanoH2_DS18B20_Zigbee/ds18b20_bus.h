// Minimal 1-Wire master plus DS18B20 support, bit-banged on a single GPIO.
//
// This exists instead of the usual OneWire/DallasTemperature pair because
// OneWire (2.3.8, the copy in ../libraries) does not build for this SoC. Its
// direct-GPIO layer (util/OneWire_direct_gpio.h) special-cases the ESP32-C3
// only; every other ESP32 falls through to the "plain ESP32" branch, which
// reads GPIO.in as a scalar and references GPIO.in1 / GPIO.out1_w1tc /
// GPIO.enable1_w1tc. On the ESP32-H2 those registers do not exist - the chip
// has 27 GPIOs - and the ones that do are register unions needing .val, so it
// is a compile error rather than a timing problem. The C3 path is exactly what
// the H2 needs, so the alternative is a patched fork of the library:
//
//   #if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6 \
//       || CONFIG_IDF_TARGET_ESP32H2 || CONFIG_IDF_TARGET_ESP32C2
//
// at each of the five #if sites. DallasTemperature itself is fine: what it
// offers beyond this file is per-sensor resolution and alarms, neither of which
// this sketch uses. Implementing the protocol here keeps the tested part in
// this repository - see test/onewire, which checks the CRC against OneWire's
// own reference table and the ROM search against a simulated bus.
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
