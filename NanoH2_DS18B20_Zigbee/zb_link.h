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
  bool valid;          // false while there is no parent entry, e.g. before a join
  uint8_t lqi;         // 0..255, higher is better
  int8_t rssi;         // dBm of the last frame received from the parent
  uint16_t parentAddr; // parent's short address, 0x0000 for the coordinator
};

// Reads the parent's entry from the neighbour table. Safe to call at any time;
// returns valid=false when the stack is not up or has no parent yet.
LinkQuality readParentLink();
