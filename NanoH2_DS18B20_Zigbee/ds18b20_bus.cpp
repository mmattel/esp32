#include "ds18b20_bus.h"

// Standard-speed 1-Wire timings, microseconds (Maxim DS18B20 datasheet).
static const uint32_t RESET_LOW_US = 500;
static const uint32_t PRESENCE_SAMPLE_US = 70;
static const uint32_t RESET_RECOVER_US = 410;
static const uint32_t WRITE1_LOW_US = 6;
static const uint32_t WRITE1_HIGH_US = 64;
static const uint32_t WRITE0_LOW_US = 60;
static const uint32_t WRITE0_HIGH_US = 10;
static const uint32_t READ_LOW_US = 6;
static const uint32_t READ_SAMPLE_US = 9;
static const uint32_t READ_RECOVER_US = 55;

// Time slots must not be stretched past their upper bound, so the parts of
// each slot that have one are run with interrupts masked. The initial low
// pulse of a bus reset has no upper bound and is left interruptible.
static portMUX_TYPE ow_mux = portMUX_INITIALIZER_UNLOCKED;

void DS18B20Bus::begin() {
  // INPUT_OUTPUT_OD: writing LOW drives the line, writing HIGH releases it to
  // the pull-up, and digitalRead() stays usable throughout. The internal
  // pull-up is only a fallback - the bus still needs its external 4.7 kOhm.
  pinMode(_pin, OUTPUT_OPEN_DRAIN | PULLUP);
  digitalWrite(_pin, HIGH);
  delayMicroseconds(RESET_RECOVER_US);
}

bool DS18B20Bus::reset() {
  // Wait for the bus to be idle high; a line stuck low means a short or a
  // missing pull-up.
  digitalWrite(_pin, HIGH);
  for (uint16_t waited = 0; digitalRead(_pin) == LOW; waited += 10) {
    if (waited >= 1000) {
      return false;
    }
    delayMicroseconds(10);
  }

  digitalWrite(_pin, LOW);
  delayMicroseconds(RESET_LOW_US);

  bool present;
  portENTER_CRITICAL(&ow_mux);
  digitalWrite(_pin, HIGH);
  delayMicroseconds(PRESENCE_SAMPLE_US);
  present = (digitalRead(_pin) == LOW);
  portEXIT_CRITICAL(&ow_mux);

  delayMicroseconds(RESET_RECOVER_US);
  return present;
}

void DS18B20Bus::writeBit(uint8_t bit) {
  portENTER_CRITICAL(&ow_mux);
  digitalWrite(_pin, LOW);
  delayMicroseconds(bit ? WRITE1_LOW_US : WRITE0_LOW_US);
  digitalWrite(_pin, HIGH);
  delayMicroseconds(bit ? WRITE1_HIGH_US : WRITE0_HIGH_US);
  portEXIT_CRITICAL(&ow_mux);
}

uint8_t DS18B20Bus::readBit() {
  uint8_t bit;
  portENTER_CRITICAL(&ow_mux);
  digitalWrite(_pin, LOW);
  delayMicroseconds(READ_LOW_US);
  digitalWrite(_pin, HIGH);
  delayMicroseconds(READ_SAMPLE_US);
  bit = digitalRead(_pin) ? 1 : 0;
  portEXIT_CRITICAL(&ow_mux);
  delayMicroseconds(READ_RECOVER_US);
  return bit;
}

void DS18B20Bus::writeByte(uint8_t value) {
  for (uint8_t i = 0; i < 8; i++) {
    writeBit((value >> i) & 0x01);
  }
}

uint8_t DS18B20Bus::readByte() {
  uint8_t value = 0;
  for (uint8_t i = 0; i < 8; i++) {
    value |= readBit() << i;
  }
  return value;
}

void DS18B20Bus::writeRom(uint64_t rom) {
  for (uint8_t i = 0; i < 8; i++) {
    writeByte((rom >> (8 * i)) & 0xFF);
  }
}

uint8_t DS18B20Bus::crc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0;
  while (len--) {
    uint8_t byte = *data++;
    for (uint8_t i = 0; i < 8; i++) {
      uint8_t mix = (crc ^ byte) & 0x01;
      crc >>= 1;
      if (mix) {
        crc ^= 0x8C;
      }
      byte >>= 1;
    }
  }
  return crc;
}

void DS18B20Bus::searchReset() {
  memset(_searchRom, 0, sizeof(_searchRom));
  _lastDiscrepancy = 0;
  _lastDeviceFlag = false;
}

// Maxim application note 187 ROM search.
bool DS18B20Bus::searchNext(uint8_t *rom) {
  if (_lastDeviceFlag) {
    searchReset();
    return false;
  }

  if (!reset()) {
    searchReset();
    return false;
  }

  writeByte(CMD_SEARCH_ROM);

  uint8_t bitNumber = 1;
  uint8_t lastZero = 0;
  uint8_t byteNumber = 0;
  uint8_t byteMask = 1;
  bool found = false;

  do {
    uint8_t idBit = readBit();
    uint8_t cmpIdBit = readBit();

    if (idBit && cmpIdBit) {
      // No device responded on either polarity: the bus went away mid-search.
      break;
    }

    uint8_t direction;
    if (idBit != cmpIdBit) {
      direction = idBit;
    } else {
      if (bitNumber < _lastDiscrepancy) {
        direction = (_searchRom[byteNumber] & byteMask) ? 1 : 0;
      } else {
        direction = (bitNumber == _lastDiscrepancy) ? 1 : 0;
      }
      if (!direction) {
        lastZero = bitNumber;
      }
    }

    if (direction) {
      _searchRom[byteNumber] |= byteMask;
    } else {
      _searchRom[byteNumber] &= ~byteMask;
    }
    writeBit(direction);

    bitNumber++;
    byteMask <<= 1;
    if (byteMask == 0) {
      byteNumber++;
      byteMask = 1;
    }
  } while (byteNumber < 8);

  if (bitNumber >= 65) {
    _lastDiscrepancy = lastZero;
    _lastDeviceFlag = (_lastDiscrepancy == 0);
    found = true;
  }

  if (!found || _searchRom[0] == 0) {
    searchReset();
    return false;
  }

  if (crc8(_searchRom, 7) != _searchRom[7]) {
    // Corrupted ROM code; abandon this pass rather than report a bad address.
    searchReset();
    return false;
  }

  memcpy(rom, _searchRom, 8);
  return true;
}

uint8_t DS18B20Bus::discover(uint64_t *roms, uint8_t maxRoms) {
  searchReset();

  uint8_t count = 0;
  uint8_t rom[8];
  // The search walks the whole 64-bit tree; cap the iterations so a wiring
  // fault cannot spin here forever.
  for (uint8_t guard = 0; guard < 32 && count < maxRoms; guard++) {
    if (!searchNext(rom)) {
      break;
    }
    if (rom[0] != FAMILY_DS18B20) {
      continue;  // some other 1-Wire device sharing the bus
    }
    uint64_t packed = 0;
    for (uint8_t i = 0; i < 8; i++) {
      packed |= (uint64_t)rom[i] << (8 * i);
    }
    roms[count++] = packed;
  }
  return count;
}

bool DS18B20Bus::setResolution12bit(uint64_t rom) {
  if (!reset()) {
    return false;
  }
  writeByte(CMD_MATCH_ROM);
  writeRom(rom);
  writeByte(CMD_WRITE_SCRATCHPAD);
  writeByte(0x4B);  // TH alarm, unused
  writeByte(0x46);  // TL alarm, unused
  writeByte(0x7F);  // config: 12-bit resolution
  return true;
}

bool DS18B20Bus::startConversionAll() {
  if (!reset()) {
    return false;
  }
  writeByte(CMD_SKIP_ROM);
  writeByte(CMD_CONVERT_T);
  return true;
}

DS18B20Reading DS18B20Bus::read(uint64_t rom) {
  DS18B20Reading out = {false, NAN};

  if (!reset()) {
    return out;
  }
  writeByte(CMD_MATCH_ROM);
  writeRom(rom);
  writeByte(CMD_READ_SCRATCHPAD);

  uint8_t sp[9];
  for (uint8_t i = 0; i < 9; i++) {
    sp[i] = readByte();
  }

  if (crc8(sp, 8) != sp[8]) {
    return out;  // also covers an absent sensor, which reads back all 0xFF
  }

  int16_t raw = (int16_t)(((uint16_t)sp[1] << 8) | sp[0]);
  float celsius = raw * 0.0625f;
  if (celsius < -55.0f || celsius > 125.0f) {
    return out;
  }

  out.valid = true;
  out.celsius = celsius;
  return out;
}

String DS18B20Bus::romToString(uint64_t rom) {
  char buf[17];
  for (uint8_t i = 0; i < 8; i++) {
    sprintf(&buf[i * 2], "%02X", (unsigned)((rom >> (8 * i)) & 0xFF));
  }
  buf[16] = '\0';
  return String(buf);
}
