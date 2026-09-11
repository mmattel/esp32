// Host tests for the 1-Wire master and DS18B20 driver.
//
// The driver is compiled against the stub Arduino.h in this directory, which
// redirects digitalWrite/digitalRead/delayMicroseconds into the simulated bus
// below. The simulator reconstructs 1-Wire slot semantics from the call
// sequence and the recorded low-pulse length, and answers as a collective of
// slaves, so the real ROM search and CRC code is exercised end to end.

#include "Arduino.h"
#include <vector>
#include <array>
#include <cassert>

// ---------------- simulated collective of 1-Wire slaves ----------------
static std::vector<std::array<uint8_t, 8>> g_devices;
static std::vector<bool> g_active;

enum SimMode { M_IDLE, M_CMD, M_SEARCH };
static SimMode g_mode = M_IDLE;
static int g_cmdBits = 0, g_cmdVal = 0;
static int g_searchBit = 0, g_searchPhase = 0;

static int g_line = 1;         // as driven by the master
static uint32_t g_lastDelay = 0;
static uint32_t g_lowDuration = 0;
static bool g_slotPending = false;   // a 6us low slot happened: read or write-1

static void sim_activateAll() {
  g_active.assign(g_devices.size(), true);
}
// Clear leftover slot state so one test case cannot bleed into the next.
static void sim_init() {
  g_mode = M_IDLE; g_cmdBits = g_cmdVal = 0;
  g_searchBit = g_searchPhase = 0;
  g_line = 1; g_lastDelay = 0; g_lowDuration = 0; g_slotPending = false;
  sim_activateAll();
}
static int devBit(size_t d, int bitIndex) {
  return (g_devices[d][bitIndex / 8] >> (bitIndex % 8)) & 1;
}

// The master released the line after driving it low for g_lowDuration us.
static void sim_slotEnd() {
  if (g_lowDuration >= 400) {            // bus reset
    sim_activateAll();
    g_mode = M_CMD;
    g_cmdBits = g_cmdVal = 0;
    g_slotPending = false;
    return;
  }
  if (g_lowDuration >= 40) {             // write-0
    g_slotPending = false;
    if (g_mode == M_CMD) {
      g_cmdBits++;                       // bit value 0
      if (g_cmdBits == 8) {
        if (g_cmdVal == 0xF0) { g_mode = M_SEARCH; g_searchBit = 0; g_searchPhase = 0; }
        else g_mode = M_IDLE;
      }
    } else if (g_mode == M_SEARCH && g_searchPhase == 2) {
      for (size_t d = 0; d < g_devices.size(); d++)
        if (g_active[d] && devBit(d, g_searchBit) != 0) g_active[d] = false;
      if (++g_searchBit == 64) g_mode = M_IDLE; else g_searchPhase = 0;
    }
    return;
  }
  g_slotPending = true;                  // short low: read slot or write-1
}

// Resolve a pending short slot as a write-1 once the next slot starts.
static void sim_resolvePendingAsWrite1() {
  if (!g_slotPending) return;
  g_slotPending = false;
  if (g_mode == M_CMD) {
    g_cmdVal |= 1 << g_cmdBits;
    g_cmdBits++;
    if (g_cmdBits == 8) {
      if (g_cmdVal == 0xF0) { g_mode = M_SEARCH; g_searchBit = 0; g_searchPhase = 0; }
      else g_mode = M_IDLE;
    }
  } else if (g_mode == M_SEARCH && g_searchPhase == 2) {
    for (size_t d = 0; d < g_devices.size(); d++)
      if (g_active[d] && devBit(d, g_searchBit) != 1) g_active[d] = false;
    if (++g_searchBit == 64) g_mode = M_IDLE; else g_searchPhase = 0;
  }
}

void sim_delay(uint32_t us) { g_lastDelay = us; }

void sim_write(uint8_t, uint8_t level) {
  if (level == LOW) {
    sim_resolvePendingAsWrite1();
    g_line = 0;
    g_lastDelay = 0;
  } else {
    if (g_line == 0) { g_lowDuration = g_lastDelay; sim_slotEnd(); }
    g_line = 1;
  }
}

int sim_read(uint8_t) {
  if (g_line == 0) return 0;             // idle-check while master holds low
  if (g_lowDuration >= 400) {            // presence pulse after a reset
    return g_devices.empty() ? 1 : 0;
  }
  if (g_slotPending) {                   // this short slot was a read slot
    g_slotPending = false;
    if (g_mode == M_SEARCH) {
      int bit;
      if (g_searchPhase == 0) {
        bit = 1;
        for (size_t d = 0; d < g_devices.size(); d++)
          if (g_active[d] && devBit(d, g_searchBit) == 0) bit = 0;
        g_searchPhase = 1;
      } else if (g_searchPhase == 1) {
        bit = 1;
        for (size_t d = 0; d < g_devices.size(); d++)
          if (g_active[d] && devBit(d, g_searchBit) == 1) bit = 0;
        g_searchPhase = 2;
      } else bit = 1;
      return bit;
    }
    return 1;
  }
  return 1;
}

#define private public
#include "../../NanoH2_DS18B20_Zigbee/ds18b20_bus.cpp"
#undef private

// ---------------- reference CRC from PaulStoffregen/OneWire ----------------
static uint8_t ref_crc8(const uint8_t *addr, uint8_t len) {
  uint8_t crc = 0;
  while (len--) {
    uint8_t inbyte = *addr++;
    for (uint8_t i = 8; i; i--) {
      uint8_t mix = (crc ^ inbyte) & 0x01;
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      inbyte >>= 1;
    }
  }
  return crc;
}

static std::array<uint8_t, 8> makeRom(uint8_t a, uint8_t b, uint8_t c) {
  std::array<uint8_t, 8> r = {0x28, a, b, c, 0x00, 0x00, 0x00, 0x00};
  r[7] = ref_crc8(r.data(), 7);
  return r;
}

int main() {
  int fails = 0;

  // 1. CRC-8 agrees with the OneWire reference on random-ish vectors.
  for (int t = 0; t < 256; t++) {
    uint8_t buf[7];
    for (int i = 0; i < 7; i++) buf[i] = (uint8_t)(t * 31 + i * 17);
    if (DS18B20Bus::crc8(buf, 7) != ref_crc8(buf, 7)) { printf("CRC mismatch at %d\n", t); fails++; break; }
  }
  printf("crc8 vs OneWire reference: %s\n", fails ? "FAIL" : "ok");

  // 2. ROM search finds exactly the devices on the bus.
  g_devices = {makeRom(0x11, 0x22, 0x33), makeRom(0xAB, 0xCD, 0xEF), makeRom(0x01, 0x00, 0x80)};
  sim_init();

  DS18B20Bus bus(2);
  uint64_t found[8];
  uint8_t n = bus.discover(found, 8);
  printf("discover() returned %u (expected %zu)\n", n, g_devices.size());
  if (n != g_devices.size()) fails++;

  for (auto &dev : g_devices) {
    uint64_t packed = 0;
    for (int i = 0; i < 8; i++) packed |= (uint64_t)dev[i] << (8 * i);
    bool hit = false;
    for (uint8_t i = 0; i < n; i++) if (found[i] == packed) hit = true;
    printf("  %s %s\n", DS18B20Bus::romToString(packed).c_str(), hit ? "found" : "MISSING");
    if (!hit) fails++;
  }

  // 3. Empty bus -> nothing found, no hang.
  g_devices.clear(); sim_init();
  DS18B20Bus empty(2);
  n = empty.discover(found, 8);
  printf("empty bus: discover() = %u (expected 0)\n", n);
  if (n != 0) fails++;

  // 4. Single device.
  g_devices = {makeRom(0x77, 0x66, 0x55)}; sim_init();
  DS18B20Bus one(2);
  n = one.discover(found, 8);
  printf("single device: discover() = %u (expected 1)\n", n);
  if (n != 1) fails++;

  // 5. Adversarial set: eight devices sharing long ROM prefixes.
  g_devices.clear();
  for (int i = 0; i < 8; i++) {
    std::array<uint8_t, 8> r = {0x28, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, (uint8_t)(0xA0 | i), 0};
    r[7] = ref_crc8(r.data(), 7);
    g_devices.push_back(r);
  }
  sim_init();
  DS18B20Bus many(2);
  n = many.discover(found, 8);
  printf("8 shared-prefix devices: discover() = %u (expected 8)\n", n);
  if (n != 8) fails++;

  // 6. Mixed families: only the 0x28 devices are reported.
  g_devices.clear();
  { std::array<uint8_t, 8> r = {0x10, 0x01, 0x02, 0x03, 0, 0, 0, 0};  // DS18S20
    r[7] = ref_crc8(r.data(), 7); g_devices.push_back(r); }
  g_devices.push_back(makeRom(0x44, 0x33, 0x22));
  sim_init();
  DS18B20Bus mixed(2);
  n = mixed.discover(found, 8);
  printf("mixed families: discover() = %u (expected 1)\n", n);
  if (n != 1) fails++;

  printf("\n%s\n", fails ? "FAILURES" : "ALL PASS");
  return fails != 0;
}
