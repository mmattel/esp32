// What the sensor slots are doing, and the lines that say so.
//
// A slot is one of four things: reading, on the bus but unreadable, assigned once
// and now missing, or never used at all. Telling those apart is the whole point of
// this file - a temperature endpoint with no value looks identical in all four
// cases, so the console and the mirror are where the difference is stated. See
// "Telling an empty slot from a sensor that has failed" in README.md.
//
// It sits in a file of its own rather than in the .ino because the rules here are
// decisions, not plumbing: which line a failed read produces, how often the
// power-on default is worth saying, and when the summary is repeated. The host
// tests in ../test/slots exercise them; nothing in the .ino can be tested at all.
//
// Nothing here touches Zigbee. Lines go out through logEvent(), which prints them
// and hands them to the mirror endpoint - see console.h. The slot arrays stay with
// the rest of the sketch's state and are passed in, because the bus scan is what
// maintains them.
//
// Every slot argument is an index below MAX_DS18B20_SENSORS; the callers are loops
// over exactly that, and there is no check here that would turn a bug into silence.

#pragma once

#include <Arduino.h>

// Enough for the longest age ageText() can produce, terminator included: at the
// far end of a uint32_t of milliseconds that is "1193 h 2 min".
#define AGE_TEXT_CHARS 16

// Sets every slot's history to "nothing read yet". Called once, from setup().
// Deliberately not called on a join: what the coordinator knows is forgotten then,
// what the hardware has done is not.
void slotsBegin();

// One line naming the configured slot count and how the slots divide between
// answering the bus, assigned but missing, and never used. Sent when those counts
// change - which includes the first call after boot - and suppressed when they
// have not, so an unchanged bus costs nothing. rom[] and present[] are the
// sketch's slot arrays, MAX_DS18B20_SENSORS long.
void slotsReportSummary(const uint64_t *rom, const bool *present);

// Forget what was last reported, so the next slotsReportSummary() says it again
// whether or not anything moved. Called on a join: the line that went out before
// the radio was up reached nobody.
void slotsForgetSummary();

// A reading that came back valid. Records it as the slot's last good one, and
// reports a reading of exactly the power-on default - once per spell of it, so a
// sensor sitting there costs one line rather than one per interval.
void slotsNoteReading(uint8_t slot, const char *rom, float celsius, bool powerOnReset);

// A read that failed every attempt. Says whether the slot ever read at all, which
// is what separates a sensor that has never worked from one that worked until a
// moment ago.
void slotsReportReadFailure(uint8_t slot, const char *rom);

// The slot has lost its sensor, whether it failed its reads or stopped answering
// the bus scan. Clears the power-on-default one-shot, so a sensor that comes back
// still at 85 C says so again; keeps the last good reading, which is still the best
// answer to "when did this last work".
void slotsSensorGone(uint8_t slot);

// A duration as an age for a console line: "17 s", "42 min", "3 h 5 min". Writes
// at most cap bytes including the terminator. Printed, never parsed.
void ageText(uint32_t ms, char *out, size_t cap);
