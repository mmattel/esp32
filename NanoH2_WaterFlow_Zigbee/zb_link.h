// Link quality towards the parent, as this device measures it.
//
// This is not the number a coordinator shows. Zigbee2MQTT's "linkquality" is the
// *coordinator's* view: how well it hears this device. The reverse direction -
// how well this device hears its parent - is only known here, and it is the half
// that decides whether a setting write, a read or an OTA ever arrives. The two
// are often not symmetric, which is exactly why the device-side value is worth
// having when deciding where to mount the board.
//
// The stack keeps it in the network neighbour table. An end device has one
// neighbour that matters, its parent.

#pragma once

#include <Arduino.h>

struct LinkQuality {
  bool valid;          // false while there is no usable parent entry, e.g. before a join
  uint8_t lqi;         // 0..255, higher is better
  int8_t rssi;         // dBm of the last frame received from the parent
  uint16_t parentAddr; // parent's short address, 0x0000 for the coordinator
  uint8_t entries;     // how many entries the neighbour table held, parent or not
  bool assumed;        // nothing was flagged as the parent; the sole entry was taken as one
  // The parent's entry is there but holds no measurement yet, so valid is false and
  // parentAddr is the only field worth reading. Told apart from having no parent at
  // all because it is a different wait: the parent is known, the radio has simply
  // not heard a frame from it since the entry was created.
  bool unmeasured;
  // The parent entry's fields as the stack holds them, measured or not - for the
  // console while a measurement is missing, never for publishing. All 0 when no
  // parent entry was found.
  uint8_t rawLqi;
  int8_t rawRssi;
  uint8_t rawAge;  // the stack's age counter for the entry
  // lqi is measured but rssi is not. Nothing to publish for the RSSI then. Not set
  // by anything at present - the RSSI follows from the LQI - and kept so a source
  // that cannot give both has a way to say so.
  bool rssiUnknown;
};

// Reads the parent's entry from the neighbour table. Safe to call at any time;
// returns valid=false when the stack is not up or has no parent yet.
//
// An end device has exactly one neighbour that can be there at all, the parent it
// joined through, so a table holding a single entry that is not flagged as the
// parent is still that link - reported with assumed set, since the relationship
// is the stack's own bookkeeping and not something to depend on.
LinkQuality readParentLink();

// Asks the parent for its IEEE address, for the answer rather than the address: the
// answer is a frame from the parent, and a frame heard from the parent is the only
// thing that puts a measurement into its neighbour entry. An end device that sends
// reports with no default response asked for may otherwise hear nothing from its
// parent for minutes, and readParentLink() then keeps finding the entry unmeasured
// or stale. One frame each way over the single hop to the parent - never through
// the coordinator. Returns whether the request was handed to the stack.
bool probeParent(uint16_t parentAddr);

// Whether the answer to the last probeParent() has come in since it was sent, and if
// so its ZDP status (0x00 success, 0x85 timeout, ...) and how many milliseconds it
// took. The answer is recorded by the stack's task; this reads it. For the console
// - which of "not sent", "not answered" and "answered but nothing measured" it is.
bool probeAnswer(uint8_t *status, uint32_t *afterMs);

// The frame that carried the parent's answer to the last probeParent(), caught as it
// arrived. This is the measurement, not the neighbour entry: on this device the stack
// leaves the parent's entry at LQI 0 / RSSI +127 however many frames the parent sends
// (field log of 2026-10-11 17:09).
//
// The LQI is the stack's own for that frame (APSDE-DATA.indication), so it is the
// parent's beyond doubt. The indication carries no RSSI, and none is needed: the
// stack computes the LQI from the RSSI - see rssiFromLqi(). (Reading the radio
// driver's "most recent" RSSI instead gave noise-floor values, LQI 7-11 at -101 to
// -121 dBm, in every one of five captures of 2026-10-11 17:30.)
struct ProbeFrame {
  uint8_t lqi;   // the answer frame's LQI, from the indication
  int8_t rssi;   // the RSSI that LQI was computed from - see rssiFromLqi()
  bool clipped;  // LQI 0 or 255: rssi is only a bound, at or below -80 / at or above -30
};
// False until the parent's answer to the last probeParent() has been seen.
bool probeFrame(ProbeFrame *out);

// The inverse of the stack's esp_zb_rssi_to_lqi(), read from its disassembly:
//   rssi < -80 -> 0,  rssi >= -29 -> 255,  else (rssi + 80) * 255 / 50, rounded down.
// One dB is 5.1 LQI steps, so every LQI in between comes from exactly one RSSI and
// this gives it back exactly: ceil(lqi * 50 / 255) - 80. Checked against the five
// LQI/RSSI pairs the neighbour table delivered on 2026-10-11, all exact. At 0 and 255
// the RSSI is only a bound - -80 or below, -30 or above (the formula itself reaches
// 255 at -30) - and *clipped says so.
int8_t rssiFromLqi(uint8_t lqi, bool *clipped);

// Puts a handler in front of the stack's APS data indications that catches the
// parent's answer frame for probeFrame(), then hands every indication on to the
// Arduino core's own handler unchanged. Call once, after Zigbee.begin(). The
// registration itself runs later, in the stack's own task - see zb_link.cpp.
bool linkBegin();

// Whether the handler is in place yet. False until the stack's task has run the
// registration that linkBegin() scheduled.
bool linkHooked();
