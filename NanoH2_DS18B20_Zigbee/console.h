// The console, and the one line of it that also goes on the air.
//
// logEvent() prints exactly like Serial.printf() does and hands the same line to
// the mirror endpoint, which publishes it - see zb_mirror.h. What goes through it
// is what the console calls an event in its own right: the join, the link going,
// whatever the button did, an error, and an interval or a delta that changed.
//
// What does not is the periodic work - the readings, the link polls, the bus
// rescans, the join hints. Those already have LOG_EVERY_READING to decide how
// much of them is printed, they are what the temperature and link endpoints are
// for, and a mirror carrying them would show nothing but the last temperature.
//
// This declaration sits in a header of its own so that a file which only needs to
// say something - zb_setting.cpp does, for the two settings a coordinator can
// write - is not made to know about the Zigbee endpoint behind it, and so the host
// tests in ../test keep compiling those files against a stub of a few lines.

#pragma once

// printf style. The console gets the whole line; the mirror keeps the first
// MIRROR_TEXT_LEN characters of it, without any leading spaces, since the console
// indents a line under the header it belongs to and the mirror carries one line
// on its own.
//
// The format attribute is what makes the compiler check these calls like it
// checks a printf: they carry the values of a fault at the moment of the fault,
// which is the last place a silent mismatch should be possible.
void logEvent(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
