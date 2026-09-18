// An endpoint that carries the last console line worth an event, as text.
//
// ZCL has no cluster for text and no coordinator subscribes to one, so the line
// travels in an attribute of its own - a character string - and that attribute is
// added to a standard analog input cluster instead of a private cluster of its
// own. That is deliberate. A report is addressed through the binding table (see
// "An expose that stays N/A" in README.md), a coordinator binds the clusters it
// recognises, and a cluster it has never heard of is one it will not bind: the
// reports would be dropped here, at the source, before the radio ever sees them.
// An analog input is bound by every coordinator, so the text rides along on a
// cluster that is bound anyway.
//
// The value of that cluster is not wasted either: it counts the lines, so it is
// the sequence number of the text beside it. It is a number, which means it needs
// no converter to be visible, and it changes with every new line, which is what an
// automation can trigger on even where the text itself is not readable.
//
// Nothing here can be written: an analog input is read-only by definition, and the
// text attribute is created read-only as well.

#pragma once

#include <Arduino.h>
#include "Zigbee.h"
#include "config.h"
#include "console.h"

// Every knob this endpoint has lives in config.h, and a config.h from before the
// endpoint existed has none of them. Unchecked, that mismatch is some thirty errors
// across three files: the first of them lands in the class below, which then has no
// members, which makes every method that touches one fail too. Said once, here, it
// is one line naming the file to update.
#if !defined(EP_MIRROR) || !defined(ZB_MIRROR_ENDPOINT) || !defined(MIRROR_TEXT_LEN) \
    || !defined(MIRROR_TEXT_ATTR_ID) || !defined(MIRROR_REPORT_MIN_INTERVAL_S) \
    || !defined(MIRROR_REPORT_HEARTBEAT_S)
#error "config.h has no 'Console mirror' section - update the whole sketch folder from one commit"
#endif

class ZbMirror : public ZigbeeAnalog {
public:
  explicit ZbMirror(uint8_t endpoint) : ZigbeeAnalog(endpoint) {}

  // Call after addAnalogInput() and before Zigbee.begin() to create the text
  // attribute. Optional: without it the endpoint still counts the lines, it just
  // cannot carry them, so a failure is a warning, not fatal.
  bool addText();

  // Takes one line, already printed by logEvent(), and publishes it when it
  // differs from the line the coordinator was last given.
  void mirror(const char *line);

  // Forgets what the coordinator was given, so the next line goes out even if it
  // repeats the last one. For a join, where whatever was published belonged to
  // the previous network - or to nobody at all.
  void forgetPublished();

  // Repeats the current line every MIRROR_REPORT_HEARTBEAT_S. Call from loop().
  void handleReports();

private:
  // Puts _text and the sequence number on the air, and remembers that it did.
  void publish();
  bool setText();
  bool reportText();

  char _text[MIRROR_TEXT_LEN + 1] = "";       // the current line, already cut to length
  char _published[MIRROR_TEXT_LEN + 1] = "";  // the line the coordinator was given
  uint16_t _sequence = 0;
  uint32_t _lastReportMs = 0;
  bool _hasText = false;  // whether addText() got the attribute created
};

// The one mirror there is, defined beside the other endpoint objects in the
// sketch. logEvent() writes to it, which is why that function needs no argument
// for it - see console.h.
extern ZbMirror zbMirror;
