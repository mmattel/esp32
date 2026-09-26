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
};

// Reads the parent's entry from the neighbour table. Safe to call at any time;
// returns valid=false when the stack is not up or has no parent yet.
//
// An end device has exactly one neighbour that can be there at all, the parent it
// joined through, so a table holding a single entry that is not flagged as the
// parent is still that link - reported with assumed set, since the relationship
// is the stack's own bookkeeping and not something to depend on.
LinkQuality readParentLink();
