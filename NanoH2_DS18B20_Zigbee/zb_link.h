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

// LQI and RSSI of the frame that carried the parent's answer to the last
// probeParent(), read from the radio driver the moment the answer came in. False
// until a successful answer has arrived since that probe.
//
// This is the measurement, not the neighbour entry: on this device the stack leaves
// the parent's entry at LQI 0 / RSSI +127 however many frames the parent sends (field
// log of 2026-10-11 17:09: seven answers, status 0x00, entry unchanged). The radio
// driver keeps both values for the most recent frame it received, and right after the
// answer that is the parent's frame - an end device hears little else.
bool probeMeasurement(uint8_t *lqi, int8_t *rssi);
