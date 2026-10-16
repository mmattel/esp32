#include "slots.h"

#include "config.h"
#include "console.h"

#if !defined(POR_SPIKE_DELTA_C) || !defined(POR_SPIKE_MAX_AGE_S) || !defined(POR_SPIKE_WINDOW_S) \
    || !defined(POR_SPIKE_REPORT_COUNT)
#error "config.h has no power-on reset spike settings - update the whole sketch folder from one commit"
#endif

// The last reading that came back valid, per slot, and when it did. Deliberately
// separate from the sketch's lastPublished[]: that one is what the coordinator has,
// which the deadband can hold well behind the sensor, and its timestamp is the time
// of a report rather than of a read. Telling "this sensor has never worked" from
// "this sensor worked until four minutes ago" needs the read.
//
// NAN means no valid reading since boot, which is not the same as an empty slot: a
// ROM code in NVS only says a sensor was once discovered, not that it ever answered
// with a temperature.
static float lastGoodC[DS18B20_SLOT_ARRAY_LEN];
static uint32_t lastGoodMs[DS18B20_SLOT_ARRAY_LEN] = {0};

// Whether the power-on default has already been reported for this slot.
static bool porReported[DS18B20_SLOT_ARRAY_LEN] = {false};

// Discarded power-on reset spikes in the slot's current window, and when the window
// opened. A count of 0 means no window is open.
static uint16_t spikeCount[DS18B20_SLOT_ARRAY_LEN] = {0};
static uint32_t spikeWindowMs[DS18B20_SLOT_ARRAY_LEN] = {0};

// What slotsReportSummary() last put on the air. 0xFF is not a reachable count -
// MAX_DS18B20_SENSORS caps all three - so the first call always reports.
static uint8_t lastOnBus = 0xFF;
static uint8_t lastMissing = 0xFF;
static uint8_t lastNeverSeen = 0xFF;

void slotsBegin() {
  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    lastGoodC[i] = NAN;
    lastGoodMs[i] = 0;
    porReported[i] = false;
    spikeCount[i] = 0;
  }
}

void ageText(uint32_t ms, char *out, size_t cap) {
  uint32_t seconds = ms / 1000UL;
  if (seconds < 60) {
    snprintf(out, cap, "%u s", (unsigned)seconds);
  } else if (seconds < 3600) {
    snprintf(out, cap, "%u min", (unsigned)(seconds / 60));
  } else {
    snprintf(out, cap, "%u h %u min", (unsigned)(seconds / 3600), (unsigned)((seconds % 3600) / 60));
  }
}

void slotsReportSummary(const uint64_t *rom, const bool *present) {
  uint8_t onBus = 0, missing = 0, neverSeen = 0;
  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    if (present[i]) {
      onBus++;
    } else if (rom[i] != 0) {
      missing++;
    } else {
      neverSeen++;
    }
  }

  if (onBus == lastOnBus && missing == lastMissing && neverSeen == lastNeverSeen && !LOG_EVERY_READING) {
    return;
  }
  lastOnBus = onBus;
  lastMissing = missing;
  lastNeverSeen = neverSeen;

  // The configured count is in the line because the three that follow only add up
  // to something once it is known: "0 on the bus" with nothing else stated could be
  // a build with no slots at all.
  //
  // 42 characters at three slots, against the MIRROR_TEXT_LEN of 48 that the mirror
  // carries - so it goes out whole rather than cut, which is what the wording is
  // chosen for. It is SLOT_SUMMARY_FIXED_CHARS (38) plus one digit per number; the
  // static_assert below the function checks the widest case, which is why
  // test/logevent_len takes its word for it (mirror-len: checked in code). It read
  // "temp sensors (3 slots): 3 on the bus, ..." (61 characters) until v3.1.4
  // brought MIRROR_TEXT_LEN down to what the stack accepts - see config.h.
  logEvent("%u slots: %u on bus, %u missing, %u never seen", (unsigned)MAX_DS18B20_SENSORS,
           (unsigned)onBus, (unsigned)missing, (unsigned)neverSeen);
}

// None of the four numbers can be larger than the slot count, so this is the
// longest the summary can ever get: every number as wide as the slot count. At the
// ceiling of 16 slots that is 46 characters, inside a MIRROR_TEXT_LEN of 48. A
// larger slot count or a longer wording that would get the line cut fails here.
static constexpr size_t decimalDigits(unsigned n) {
  return n < 10 ? 1 : 1 + decimalDigits(n / 10);
}
static_assert(SLOT_SUMMARY_FIXED_CHARS + 4 * decimalDigits(MAX_DS18B20_SENSORS) <= MIRROR_TEXT_LEN,
              "the slot summary can outgrow the mirrored line at this slot count - shorten the wording");

void slotsForgetSummary() {
  lastOnBus = lastMissing = lastNeverSeen = 0xFF;
}

void slotsNoteReading(uint8_t slot, const char *rom, float celsius, bool powerOnReset) {
  lastGoodC[slot] = celsius;
  lastGoodMs[slot] = millis();

  // 85.00 C is the temperature register's power-on value, so a sensor stuck at it
  // is one whose supply keeps dropping out, or one read before its first conversion
  // finished. It is also a temperature a sensor can really be at, which is why this
  // says what the value means and the reading is still published. The follow-up line
  // is console-only: the mirror holds one line, and the one worth having on the air
  // is the fault rather than the advice.
  if (!powerOnReset) {
    porReported[slot] = false;
    return;
  }
  if (porReported[slot]) {
    return;
  }
  porReported[slot] = true;
  // mirror-len: %s<=4 - romTail(), see slots.h
  logEvent("slot %u (%s): 85.00 C is the power-on default", (unsigned)slot, romTail(rom));
  Serial.printf("  check the supply and the wiring" CONSOLE_EOL);
}

bool slotsIsPorSpike(uint8_t slot, float celsius, bool powerOnReset) {
  if (!powerOnReset) {
    return false;
  }
  if (isnan(lastGoodC[slot]) || (millis() - lastGoodMs[slot]) > (uint32_t)POR_SPIKE_MAX_AGE_S * 1000UL) {
    return true;  // nothing recent to judge by
  }
  return fabsf(celsius - lastGoodC[slot]) > POR_SPIKE_DELTA_C;
}

void slotsNoteSpike(uint8_t slot, const char *rom) {
  if (spikeCount[slot] == 0) {
    spikeWindowMs[slot] = millis();
  }
  if (spikeCount[slot] < UINT16_MAX) {
    spikeCount[slot]++;
  }
  // Console only, one per spike; the mirror gets the count - see slotsReportSpikes().
  if (isnan(lastGoodC[slot])) {
    Serial.printf("slot %u (%s): 85.00 C power-on reset discarded, no good reading yet" CONSOLE_EOL,
                  (unsigned)slot, rom);
  } else {
    char age[AGE_TEXT_CHARS];
    ageText(millis() - lastGoodMs[slot], age, sizeof(age));
    Serial.printf("slot %u (%s): 85.00 C power-on reset discarded, last good %.*f C %s ago" CONSOLE_EOL,
                  (unsigned)slot, rom, TEMP_PUBLISH_DECIMALS, lastGoodC[slot], age);
  }
}

void slotsReportSpikes(uint8_t slot, const char *rom) {
  if (spikeCount[slot] == 0 || (millis() - spikeWindowMs[slot]) < (uint32_t)POR_SPIKE_WINDOW_S * 1000UL) {
    return;
  }
  if (spikeCount[slot] >= POR_SPIKE_REPORT_COUNT) {
    // mirror-len: %s<=4 - romTail(), see slots.h
    logEvent("slot %u (%s): %u spikes in %u s", (unsigned)slot, romTail(rom), (unsigned)spikeCount[slot],
             (unsigned)POR_SPIKE_WINDOW_S);
  }
  spikeCount[slot] = 0;
}

void slotsReportReadFailure(uint8_t slot, const char *rom, const char *why) {
  // Which of the two failures this is decides what to do about it: a sensor that has
  // never read since boot is wired wrong or dead, one that read fine until a moment
  // ago is a contact or a supply that is going. The age of the last good reading is
  // a console detail, because it changes on every attempt and would otherwise defeat
  // the mirror's deadband - every failed read would cost a report. The reason takes
  // the place of the words "read failed": both do not fit the mirror.
  // mirror-len: %s<=4,10 - romTail(), then DS18B20Bus::FAULT_TEXT_MAX
  if (isnan(lastGoodC[slot])) {
    logEvent("slot %u (%s): %s, never read yet", (unsigned)slot, romTail(rom), why);
    return;
  }

  // mirror-len: %s<=4,10
  logEvent("slot %u (%s): %s, last %.*f C", (unsigned)slot, romTail(rom), why, TEMP_PUBLISH_DECIMALS,
           lastGoodC[slot]);
  char age[AGE_TEXT_CHARS];
  ageText(millis() - lastGoodMs[slot], age, sizeof(age));
  Serial.printf("  that reading was %s ago" CONSOLE_EOL, age);
}

void slotsSensorGone(uint8_t slot) {
  porReported[slot] = false;
}

void slotsForgetSlot(uint8_t slot) {
  // The same state slotsBegin() sets up, for one slot: the slot is as it was at boot
  // before anything was discovered. The last good reading goes too, which is the
  // whole difference from slotsSensorGone() - a replacement sensor in this slot must
  // not be told it read 21.5 C four minutes ago, because that was a different sensor.
  lastGoodC[slot] = NAN;
  lastGoodMs[slot] = 0;
  porReported[slot] = false;
  spikeCount[slot] = 0;  // a new sensor starts with a clean record
}
