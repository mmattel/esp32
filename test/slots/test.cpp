// Host tests for the slot bookkeeping: what the summary line counts, when it is
// worth repeating, and which of the two read failures a slot is in.
//
// Two things are checked throughout, and they are not the same thing. What went
// out on the air is what went through logEvent(), which is captured below; what
// only the console saw is everything else printed. Several of the decisions in
// slots.cpp are exactly that split - the mirror carries one line, so a detail that
// changes on every attempt has to stay off it - and a test that looked at the
// console alone would pass while the mirror filled up with noise.
//
// Arduino.h in this directory is a stub: it drives millis() from hostMillis and
// keeps a copy of everything printed.

#include "Arduino.h"
#include <cstdarg>

// The slot count is pinned rather than taken from the sketch, and so is
// LOG_EVERY_READING. Both are knobs a build is expected to change - 0 slots is a
// documented configuration - and these tests are about the rules, not about the
// number they were run with. config.h is included first so the real values are
// what gets replaced; the copy slots.cpp pulls in is a no-op after this.
#include "../../NanoH2_DS18B20_Zigbee/config.h"
#undef MAX_DS18B20_SENSORS
#define MAX_DS18B20_SENSORS 4
#undef LOG_EVERY_READING
#define LOG_EVERY_READING 0

#include "../../NanoH2_DS18B20_Zigbee/slots.cpp"

uint32_t hostMillis = 0;

// What went on the air: one entry per logEvent(), which on the device prints the
// line and hands the same text to the mirror endpoint (see zb_mirror.cpp). There
// is no endpoint here, so this is the printing half plus a record of what was
// offered - which is what the mirror would have published.
static const int MAX_LINES = 32;
static char mirrored[MAX_LINES][256];
static int mirrorCount = 0;

// Everything printed, in order, mirrored lines included.
static char console[8192];

void consoleCapture(const char *text) {
  size_t used = strlen(console);
  snprintf(console + used, sizeof(console) - used, "%s", text);
}

void logEvent(const char *fmt, ...) {
  char line[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  if (mirrorCount < MAX_LINES) {
    strcpy(mirrored[mirrorCount++], line);
  }
  consoleCapture(line);
  consoleCapture("\n");
  printf("%s\n", line);
}

static int fails = 0;
static void check(const char *what, bool ok) {
  printf("  %-56s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) fails++;
}

static void reset() {
  mirrorCount = 0;
  console[0] = '\0';
  hostMillis = 0;
  slotsBegin();
  slotsForgetSummary();
}

static const char *lastMirrored() {
  return mirrorCount ? mirrored[mirrorCount - 1] : "";
}

// On the air at all, in any of the lines offered since the last reset().
static bool onAir(const char *fragment) {
  for (int i = 0; i < mirrorCount; i++) {
    if (strstr(mirrored[i], fragment)) {
      return true;
    }
  }
  return false;
}

static bool printed(const char *fragment) {
  return strstr(console, fragment) != nullptr;
}

// The two slot arrays the sketch keeps and passes in, in the shape the scan leaves
// them: a ROM code means a sensor was once discovered for that slot, present means
// it answered the last search.
static uint64_t rom[MAX_DS18B20_SENSORS];
static bool present[MAX_DS18B20_SENSORS];

static void slotsEmpty() {
  for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
    rom[i] = 0;
    present[i] = false;
  }
}

static const char *ROM_A = "28FF641E1234ABCD";
static const char *ROM_B = "2800000BDEADBEEF";

int main() {
  printf("what the summary counts\n");
  reset();
  {
    slotsEmpty();
    slotsReportSummary(rom, present);
    check("a fresh device: every slot never seen",
          strcmp(lastMirrored(), "temp sensors (4 slots): 0 on the bus, 0 missing, 4 never seen") == 0);

    // Two sensors found, of which one has since stopped answering. The third slot
    // holds a ROM code as well but its sensor never turned up this boot; the fourth
    // has never been used at all. All four states in one line.
    reset();
    rom[0] = 0x28FF641E1234ABCDULL;
    present[0] = true;
    rom[1] = 0x2800000BDEADBEEFULL;
    present[1] = false;
    rom[2] = 0x280000000000BEEFULL;
    present[2] = false;
    slotsReportSummary(rom, present);
    check("assigned and answering counts as on the bus",
          strcmp(lastMirrored(), "temp sensors (4 slots): 1 on the bus, 2 missing, 1 never seen") == 0);

    // The numbers have to add up to the configured count whatever the state, since
    // that is what makes the line readable without knowing the build.
    reset();
    slotsEmpty();
    for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
      rom[i] = 0x2800000000000001ULL + i;
      present[i] = true;
    }
    slotsReportSummary(rom, present);
    check("every slot filled",
          strcmp(lastMirrored(), "temp sensors (4 slots): 4 on the bus, 0 missing, 0 never seen") == 0);
  }

  printf("the summary fits the line the mirror carries\n");
  reset();
  {
    slotsEmpty();
    for (uint8_t i = 0; i < MAX_DS18B20_SENSORS; i++) {
      rom[i] = 0x2800000000000001ULL + i;
      present[i] = true;
    }
    slotsReportSummary(rom, present);

    // The one line of this sketch's output that is built from four numbers, and the
    // one that has overflowed while being worded. 57 characters plus a digit per
    // number, so the check is on the budget rather than on today's value: a rewording
    // that spends the slack fails here instead of arriving truncated at the
    // coordinator. A build of more than 9 slots spends one more character on each
    // count that reaches two digits.
    size_t len = strlen(lastMirrored());
    check("57 characters plus one digit per number", len == 57 + 4);
    check("within MIRROR_TEXT_LEN, so it goes out whole", len <= MIRROR_TEXT_LEN);
  }

  printf("the summary is repeated only when it changed\n");
  reset();
  {
    slotsEmpty();
    rom[0] = 0x28FF641E1234ABCDULL;
    present[0] = true;
    slotsReportSummary(rom, present);
    slotsReportSummary(rom, present);
    slotsReportSummary(rom, present);
    check("an unchanged bus costs one line, not three", mirrorCount == 1);

    // The counts are what is compared, not the slots: a sensor moving from one slot
    // to another is a scan detail, and the console line for it is where that belongs.
    rom[1] = 0x2800000BDEADBEEFULL;
    present[1] = true;
    slotsReportSummary(rom, present);
    check("a sensor turning up is reported", mirrorCount == 2 && onAir("2 on the bus, 0 missing, 2 never seen"));

    present[1] = false;
    slotsReportSummary(rom, present);
    check("and so is one going missing", mirrorCount == 3 && onAir("1 on the bus, 1 missing, 2 never seen"));

    // A join: the line that went out before the radio was up reached nobody, so the
    // summary has to be sendable again without anything on the bus having moved.
    slotsForgetSummary();
    slotsReportSummary(rom, present);
    check("a join sends the same summary again", mirrorCount == 4);
    check("with the same text", strcmp(mirrored[2], mirrored[3]) == 0);
    slotsReportSummary(rom, present);
    check("and the deadband holds from there", mirrorCount == 4);
  }

  printf("a reading of exactly the power-on default\n");
  reset();
  {
    slotsNoteReading(0, ROM_A, 85.0f, true);
    check("said once, with the slot and the sensor",
          mirrorCount == 1
              && strcmp(lastMirrored(), "slot 0 (28FF641E1234ABCD): 85.00 C is the power-on default") == 0);
    check("what to do about it is printed", printed("check the supply and the wiring"));
    // The mirror holds one line; the fault is worth the air, the advice is not.
    check("and stays off the air", !onAir("check the supply"));

    slotsNoteReading(0, ROM_A, 85.0f, true);
    slotsNoteReading(0, ROM_A, 85.0f, true);
    check("a sensor sitting there costs one line, not one per reading", mirrorCount == 1);

    // Still one-shot per spell of it: the sensor recovering and dropping out again is
    // a new fault, and a new line.
    slotsNoteReading(0, ROM_A, 21.5f, false);
    check("a real reading is not a line of its own", mirrorCount == 1);
    slotsNoteReading(0, ROM_A, 85.0f, true);
    check("back at the default -> said again", mirrorCount == 2);

    // Same, the other way round: the sensor disappears while stuck at 85 C and comes
    // back still stuck at it. Nothing in between reset the one-shot but the slot
    // losing its sensor, which is what slotsSensorGone() is for.
    slotsSensorGone(0);
    slotsNoteReading(0, ROM_A, 85.0f, true);
    check("a sensor that comes back still at 85 C says so", mirrorCount == 3);

    // Per slot, not per device: two sensors both at the default are two faults.
    reset();
    slotsNoteReading(0, ROM_A, 85.0f, true);
    slotsNoteReading(1, ROM_B, 85.0f, true);
    check("counted per slot",
          mirrorCount == 2 && onAir("slot 0 (28FF641E1234ABCD)") && onAir("slot 1 (2800000BDEADBEEF)"));
  }

  printf("a read that failed every attempt\n");
  reset();
  {
    slotsReportReadFailure(0, ROM_A);
    check("never read since boot, so the wiring is the suspect",
          strcmp(lastMirrored(), "slot 0 (28FF641E1234ABCD): read failed, never read since boot") == 0);
    check("nothing to say about an age", !printed("ago"));

    // A slot that was reading until a moment ago is a different fault with a
    // different answer, and the last good value is the useful part of it.
    reset();
    hostMillis = 4000;
    slotsNoteReading(0, ROM_A, 21.5f, false);
    hostMillis = 4000 + 95 * 1000UL;
    slotsReportReadFailure(0, ROM_A);
    char want[128];
    snprintf(want, sizeof(want), "slot 0 (28FF641E1234ABCD): read failed, last good %.*f C",
             TEMP_PUBLISH_DECIMALS, 21.5f);
    check("says what it last read", strcmp(lastMirrored(), want) == 0);
    check("and how long ago that was", printed("that reading was 1 min ago"));
    // The age changes on every attempt, so on the air it would cost a report per
    // failed read - see the mirror's deadband in zb_mirror.cpp.
    check("the age stays on the console", !onAir("ago"));

    // Repeated failures are not deduplicated here: the mirror's own deadband does
    // that, and it is the line that has to stay identical for it to work.
    int before = mirrorCount;
    slotsReportReadFailure(0, ROM_A);
    check("the same failure produces the same line", mirrorCount == before + 1
                                                       && strcmp(mirrored[before], mirrored[before - 1]) == 0);

    // The slot's ROM code cannot change without a factory reset, so the last good
    // reading is this sensor's and no other's - which is why losing the sensor does
    // not throw it away.
    slotsSensorGone(0);
    slotsReportReadFailure(0, ROM_A);
    check("a sensor that went missing still has a last good reading", onAir("last good"));
    check("still without the age on the air", !onAir("ago"));

    // What the hardware has done is forgotten only on a reboot.
    slotsBegin();
    slotsReportReadFailure(0, ROM_A);
    check("after slotsBegin() there is nothing to remember", onAir("never read since boot"));

    // One slot reading tells the next one nothing.
    reset();
    slotsNoteReading(1, ROM_B, 21.5f, false);
    slotsReportReadFailure(0, ROM_A);
    check("history is per slot",
          strcmp(lastMirrored(), "slot 0 (28FF641E1234ABCD): read failed, never read since boot") == 0);
  }

  printf("how an age reads\n");
  {
    char age[AGE_TEXT_CHARS];
    struct {
      uint32_t ms;
      const char *want;
    } cases[] = {
        {0, "0 s"},
        {999, "0 s"},            // under a second, and still an age rather than blank
        {59 * 1000UL, "59 s"},
        {60 * 1000UL, "1 min"},  // seconds stop being interesting here
        {3599 * 1000UL, "59 min"},
        {3600 * 1000UL, "1 h 0 min"},
        {3660 * 1000UL, "1 h 1 min"},
        {0xFFFFFFFFUL, "1193 h 2 min"},  // 49 days, the far end of millis()
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      ageText(cases[i].ms, age, sizeof(age));
      printf("  %-56s %s\n", cases[i].want, strcmp(age, cases[i].want) == 0 ? "ok" : "FAIL");
      if (strcmp(age, cases[i].want) != 0) {
        printf("    got \"%s\"\n", age);
        fails++;
      }
    }
    // AGE_TEXT_CHARS is what the callers declare, so the longest age has to fit it
    // with room for the terminator.
    ageText(0xFFFFFFFFUL, age, sizeof(age));
    check("the longest age fits AGE_TEXT_CHARS", strlen(age) < AGE_TEXT_CHARS);

    char tiny[4];
    memset(tiny, 'z', sizeof(tiny));
    ageText(0xFFFFFFFFUL, tiny, sizeof(tiny));
    check("a buffer too small is cut, not overrun", strlen(tiny) == sizeof(tiny) - 1);
  }

  printf("\n%s\n", fails ? "FAILURES" : "ALL PASS");
  return fails ? 1 : 0;
}
