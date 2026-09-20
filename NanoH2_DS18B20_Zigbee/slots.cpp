#include "slots.h"

#include "config.h"
#include "console.h"

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
  // 61 characters at three slots, against the MIRROR_TEXT_LEN of 64 that the mirror
  // carries - so it goes out whole rather than cut, which is what the wording is
  // chosen for. It is 57 plus one digit per number, so any slot count that is
  // physically sensible on one 1-Wire bus still fits; a line of 30-odd slots with
  // two-digit counts everywhere would be the first to lose a character.
  logEvent("temp sensors (%u slots): %u on the bus, %u missing, %u never seen", (unsigned)MAX_DS18B20_SENSORS,
           (unsigned)onBus, (unsigned)missing, (unsigned)neverSeen);
}

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
  logEvent("slot %u (%s): 85.00 C is the power-on default", (unsigned)slot, rom);
  Serial.printf("  check the supply and the wiring" CONSOLE_EOL);
}

void slotsReportReadFailure(uint8_t slot, const char *rom) {
  // Which of the two failures this is decides what to do about it: a sensor that has
  // never read since boot is wired wrong or dead, one that read fine until a moment
  // ago is a contact or a supply that is going. The age of the last good reading is
  // a console detail, because it changes on every attempt and would otherwise defeat
  // the mirror's deadband - every failed read would cost a report.
  if (isnan(lastGoodC[slot])) {
    logEvent("slot %u (%s): read failed, never read since boot", (unsigned)slot, rom);
    return;
  }

  logEvent("slot %u (%s): read failed, last good %.*f C", (unsigned)slot, rom, TEMP_PUBLISH_DECIMALS,
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
}
