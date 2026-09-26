#include "zb_link.h"
#include "Zigbee.h"

// Whether an entry holds a measurement at all. The stack creates the parent's
// neighbour entry when the join completes and fills these two fields in from a frame
// it has actually heard, so a read in between finds LQI 0 with RSSI +127 - the int8
// maximum standing in for "nothing measured yet". +127 dBm is not a level a receiver
// can report: the RSSI endpoint's range stops at 0 dBm and a mesh link sits tens of
// dB below that, so this is read as no measurement rather than as a good one.
//
// Only a positive RSSI is rejected. LQI 0 on its own is a legal worst case, and so is
// an RSSI of exactly 0 as far as the range goes, so neither is evidence of anything -
// the impossible sign is.
static bool measured(const esp_zb_nwk_neighbor_info_t &neighbour) {
  return neighbour.rssi <= 0;
}

LinkQuality readParentLink() {
  LinkQuality link = {false, 0, 0, 0, 0, false, false};

  if (!Zigbee.started()) {
    return link;
  }

  // The neighbour table belongs to the Zigbee task and this runs in the Arduino
  // task, so take the stack lock - the same lock the Zigbee library takes around
  // every attribute access.
  if (!esp_zb_lock_acquire(portMAX_DELAY)) {
    log_w("Cannot read the neighbour table: failed to acquire the Zigbee lock");
    return link;
  }

  esp_zb_nwk_info_iterator_t it = ESP_ZB_NWK_INFO_ITERATOR_INIT;
  esp_zb_nwk_neighbor_info_t neighbour;
  esp_zb_nwk_neighbor_info_t only = {};
  while (esp_zb_nwk_get_next_neighbor(&it, &neighbour) == ESP_OK) {
    if (link.entries == 0) {
      only = neighbour;  // kept for the single-entry case below
    }
    link.entries++;

    // An end device should have nothing but its parent in there, but the table
    // is the stack's, not ours, so pick the entry by relationship rather than
    // trusting it to hold exactly one. The walk continues past the parent so
    // that entries is the size of the table and not just the part read.
    if (link.valid || link.unmeasured || neighbour.relationship != ESP_ZB_NWK_RELATIONSHIP_PARENT) {
      continue;
    }
    // Which parent this is, before anything is known about the link to it: it is
    // worth having on the console either way, and it is what the wait for a
    // measurement is a wait for.
    link.parentAddr = neighbour.short_addr;
    if (!measured(neighbour)) {
      link.unmeasured = true;
      continue;
    }
    link.valid = true;
    link.lqi = neighbour.lqi;
    link.rssi = neighbour.rssi;
  }

  // Nothing carried the parent relationship. With exactly one entry there is
  // still only one thing it can be - an end device has no other neighbour - so
  // report it rather than the link this device plainly has as unknown. Two or
  // more entries and there is a real choice to make, which this cannot make.
  if (!link.valid && !link.unmeasured && link.entries == 1) {
    link.parentAddr = only.short_addr;
    if (measured(only)) {
      link.valid = true;
      link.assumed = true;
      link.lqi = only.lqi;
      link.rssi = only.rssi;
    } else {
      // The same wait as above, one entry short of being sure whose link it is.
      link.unmeasured = true;
    }
  }

  esp_zb_lock_release();
  return link;
}
