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
static std::vector<std::array<uint8_t, 9>> g_scratch;   // one scratchpad per device
static std::vector<bool> g_active;

// Where the collective is in the transaction. M_CMD means the next eight bits are
// a ROM command; every command that takes a payload has a mode of its own, and
// falls back to M_CMD when the payload is done if another command may follow.
enum SimMode { M_IDLE, M_CMD, M_SEARCH, M_MATCH, M_WRITE_SP, M_READ_SP };
static SimMode g_mode = M_IDLE;
static int g_cmdBits = 0, g_cmdVal = 0;
static int g_searchBit = 0, g_searchPhase = 0;
static int g_matchBit = 0;               // ROM bits taken so far after MATCH ROM
static int g_selected = -1;              // device MATCH ROM addressed, -1 for none
static int g_writeBits = 0;              // scratchpad bytes being written at us
static int g_readBit = 0;                // scratchpad bits handed back so far

static int g_line = 1;         // as driven by the master
static uint32_t g_lastDelay = 0;
static uint32_t g_lowDuration = 0;
static bool g_slotPending = false;   // a 6us low slot happened: read or write-1

static void sim_activateAll() {
  g_active.assign(g_devices.size(), true);
}
// Clear leftover slot state so one test case cannot bleed into the next. The
// scratchpads are sized here and zeroed, so a test that does not set one gets a
// device whose reading is 0 C with a good CRC rather than uninitialised memory.
static void sim_init() {
  g_mode = M_IDLE; g_cmdBits = g_cmdVal = 0;
  g_searchBit = g_searchPhase = 0;
  g_matchBit = 0; g_selected = -1;
  g_writeBits = 0; g_readBit = 0;
  g_line = 1; g_lastDelay = 0; g_lowDuration = 0; g_slotPending = false;
  sim_activateAll();
  g_scratch.assign(g_devices.size(), std::array<uint8_t, 9>{});
}
static int devBit(size_t d, int bitIndex) {
  return (g_devices[d][bitIndex / 8] >> (bitIndex % 8)) & 1;
}

// One bit written by the master, whichever way it was signalled. Keeping the
// state machine in a single place is what stops the write-0 and the write-1 path
// from drifting apart as commands are added.
static void sim_masterBit(int bit) {
  switch (g_mode) {
    case M_CMD:
      if (bit) g_cmdVal |= 1 << g_cmdBits;
      if (++g_cmdBits < 8) return;
      switch (g_cmdVal) {
        case 0xF0: g_mode = M_SEARCH; g_searchBit = 0; g_searchPhase = 0; break;
        case 0x55: g_mode = M_MATCH; g_matchBit = 0; g_selected = -1; break;
        // SKIP ROM addresses everything and is followed by another command, so no
        // device is selected and the next eight bits are read as one.
        case 0xCC: g_mode = M_CMD; g_cmdBits = g_cmdVal = 0; g_selected = -1; return;
        case 0x4E: g_mode = M_WRITE_SP; g_writeBits = 0; break;
        case 0xBE: g_mode = M_READ_SP; g_readBit = 0; break;
        default: g_mode = M_IDLE; break;   // CONVERT T and anything unmodelled
      }
      return;

    case M_SEARCH:
      // Only the third slot of each search triple is a master bit: the first two
      // are reads, handled in sim_read().
      if (g_searchPhase != 2) return;
      for (size_t d = 0; d < g_devices.size(); d++)
        if (g_active[d] && devBit(d, g_searchBit) != bit) g_active[d] = false;
      if (++g_searchBit == 64) g_mode = M_IDLE; else g_searchPhase = 0;
      return;

    case M_MATCH:
      // Deselect as the address arrives, exactly as the devices do, so a ROM that
      // is not on the bus leaves nothing selected and the read slots come back as
      // all ones - which is what the driver sees from an absent sensor.
      for (size_t d = 0; d < g_devices.size(); d++)
        if (g_active[d] && devBit(d, g_matchBit) != bit) g_active[d] = false;
      if (++g_matchBit < 64) return;
      g_selected = -1;
      for (size_t d = 0; d < g_devices.size(); d++)
        if (g_active[d]) g_selected = (int)d;
      g_mode = M_CMD; g_cmdBits = g_cmdVal = 0;
      return;

    case M_WRITE_SP:
      // WRITE SCRATCHPAD carries TH, TL and the config byte. They are counted and
      // dropped: storing them would invalidate the CRC the test set up, and no
      // test reads them back.
      if (++g_writeBits == 24) g_mode = M_IDLE;
      return;

    default:
      return;
  }
}

// The master released the line after driving it low for g_lowDuration us.
static void sim_slotEnd() {
  if (g_lowDuration >= 400) {            // bus reset
    sim_activateAll();
    g_mode = M_CMD;
    g_cmdBits = g_cmdVal = 0;
    g_selected = -1;
    g_slotPending = false;
    return;
  }
  if (g_lowDuration >= 40) {             // write-0
    g_slotPending = false;
    sim_masterBit(0);
    return;
  }
  g_slotPending = true;                  // short low: read slot or write-1
}

// Resolve a pending short slot as a write-1 once the next slot starts.
static void sim_resolvePendingAsWrite1() {
  if (!g_slotPending) return;
  g_slotPending = false;
  sim_masterBit(1);
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
    if (g_mode == M_READ_SP) {
      // Nine bytes, LSB first, and then ones: the driver stops at nine, and a bus
      // with nobody selected reads as all ones throughout.
      if (g_selected < 0 || g_readBit >= 72) return 1;
      int bit = (g_scratch[g_selected][g_readBit / 8] >> (g_readBit % 8)) & 1;
      g_readBit++;
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

// Loads one device's scratchpad with a temperature register value. The other
// bytes are what a DS18B20 holds after the sketch has set 12-bit resolution: TH,
// TL, config, the reserved trio. goodCrc false flips the check byte, which is the
// only way to tell the driver's CRC rejection from its range rejection.
static void sim_setScratch(size_t dev, int16_t raw, bool goodCrc = true) {
  std::array<uint8_t, 9> sp = {(uint8_t)(raw & 0xFF), (uint8_t)((raw >> 8) & 0xFF),
                               0x4B, 0x46, 0x7F, 0xFF, 0x0C, 0x10, 0};
  sp[8] = ref_crc8(sp.data(), 8);
  if (!goodCrc) sp[8] ^= 0xFF;
  g_scratch[dev] = sp;
}

// A reading's three fields as one line, so a failure shows what came back rather
// than only that it was wrong.
static void showReading(const char *what, const DS18B20Reading &r) {
  printf("%s: valid=%d celsius=%.4f powerOnReset=%d\n", what, r.valid, r.celsius, r.powerOnReset);
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

  // ---- read(): MATCH ROM plus READ SCRATCHPAD against one device ----
  // Two devices throughout, so a pass also proves read() addressed the right one:
  // an implementation that ignored the ROM would answer from whichever came first.
  g_devices = {makeRom(0x01, 0x02, 0x03), makeRom(0x0A, 0x0B, 0x0C)};
  sim_init();
  DS18B20Bus rd(2);
  uint64_t romA = 0, romB = 0;
  for (int i = 0; i < 8; i++) {
    romA |= (uint64_t)g_devices[0][i] << (8 * i);
    romB |= (uint64_t)g_devices[1][i] << (8 * i);
  }

  // 7. An ordinary reading, and the addressing that gets it. 0x0158 is 344
  //    sixteenths, so 21.5 C exactly, which a float compares safely.
  sim_setScratch(0, 0x0158);
  sim_setScratch(1, 0x0640);             // 100 C, to be told apart from slot 0's
  DS18B20Reading r = rd.read(romA);
  showReading("read() 0x0158 on device A", r);
  if (!r.valid || r.celsius != 21.5f || r.powerOnReset) fails++;

  r = rd.read(romB);
  showReading("read() 0x0640 on device B", r);
  if (!r.valid || r.celsius != 100.0f || r.powerOnReset) fails++;

  // 8. The power-on default: 85 C with a good CRC, so it must come back valid and
  //    flagged. This is the case the sketch reports as "85.00 C is the power-on
  //    default" - a supply that dipped, or a read before the first conversion.
  sim_setScratch(0, 0x0550);
  r = rd.read(romA);
  showReading("read() 0x0550 (power-on default)", r);
  if (!r.valid || r.celsius != 85.0f || !r.powerOnReset) fails++;

  // 9. One sixteenth either side of it is a real temperature, not the default.
  //    The flag is an exact-value test, and this is what says so.
  sim_setScratch(0, 0x054F);
  r = rd.read(romA);
  showReading("read() 0x054F (84.9375 C)", r);
  if (!r.valid || r.powerOnReset) fails++;
  sim_setScratch(0, 0x0551);
  r = rd.read(romA);
  showReading("read() 0x0551 (85.0625 C)", r);
  if (!r.valid || r.powerOnReset) fails++;

  // 10. A negative reading: 0xFF5E is -162 sixteenths, -10.125 C.
  sim_setScratch(0, (int16_t)0xFF5E);
  r = rd.read(romA);
  showReading("read() 0xFF5E (-10.125 C)", r);
  if (!r.valid || r.celsius != -10.125f || r.powerOnReset) fails++;

  // 11. A bad CRC is rejected even though the temperature itself is plausible.
  sim_setScratch(0, 0x0158, false);
  r = rd.read(romA);
  showReading("read() with a bad CRC", r);
  if (r.valid) fails++;

  // 12. Out of range: 0x0800 is 128 C, past the DS18B20's 125 C, and its CRC is
  //     good - so this is the range check and nothing else.
  sim_setScratch(0, 0x0800);
  r = rd.read(romA);
  showReading("read() 0x0800 (128 C, out of range)", r);
  if (r.valid) fails++;

  // 13. A ROM that is not on the bus: nobody answers MATCH ROM, the read slots
  //     come back as all ones, and 0xFF x9 fails the CRC. This is what the sketch
  //     sees from a sensor that was unplugged since the last scan.
  sim_setScratch(0, 0x0158);
  r = rd.read(romA ^ 0xFF00);            // same family code, wrong serial
  showReading("read() of an absent ROM", r);
  if (r.valid) fails++;

  // 14. An empty bus fails at the reset, before any scratchpad is involved.
  g_devices.clear(); sim_init();
  DS18B20Bus gone(2);
  r = gone.read(romA);
  showReading("read() on an empty bus", r);
  if (r.valid) fails++;

  // ---- romToChars() / romToString(): the same 16 digits, two ways ----
  // 15. The buffer form is what every log line uses, so it has to agree with the
  //     String form digit for digit, and it has to terminate inside ROM_CHARS. The
  //     expected text is spelled out once: a ROM code is printed family code first,
  //     which is the low byte of the packed value, so a byte order mistake here
  //     would produce a plausible-looking address that matches nothing on the bus.
  {
    // The packed form is little-endian, so the family code 0x28 is the low byte and
    // comes out first: this is the sensor printed everywhere as 28FF641E1234ABCD.
    uint64_t packed = 0xCDAB34121E64FF28ULL;
    char buf[DS18B20Bus::ROM_CHARS];
    memset(buf, 'x', sizeof(buf));
    DS18B20Bus::romToChars(packed, buf);
    printf("romToChars(0xCDAB34121E64FF28) = %s\n", buf);
    if (strcmp(buf, "28FF641E1234ABCD") != 0) fails++;
    if (strlen(buf) != DS18B20Bus::ROM_CHARS - 1) fails++;
    if (DS18B20Bus::romToString(packed) != std::string(buf)) fails++;

    // Every digit position, both nibbles: a table with a hole in it prints the wrong
    // address for one sensor in sixteen.
    for (int nibble = 0; nibble < 16; nibble++) {
      uint64_t all = 0;
      for (int i = 0; i < 16; i++) all |= (uint64_t)nibble << (4 * i);
      DS18B20Bus::romToChars(all, buf);
      char want[DS18B20Bus::ROM_CHARS];
      for (int i = 0; i < 16; i++) want[i] = "0123456789ABCDEF"[nibble];
      want[16] = '\0';
      if (strcmp(buf, want) != 0) { printf("  nibble %X printed as %s\n", nibble, buf); fails++; }
    }
  }

  printf("\n%s\n", fails ? "FAILURES" : "ALL PASS");
  return fails != 0;
}
