#include "zb_link.h"
#include "Zigbee.h"

LinkQuality readParentLink() {
  LinkQuality link = {false, 0, 0, 0, 0, false};

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
    if (link.valid || neighbour.relationship != ESP_ZB_NWK_RELATIONSHIP_PARENT) {
      continue;
    }
    link.valid = true;
    link.lqi = neighbour.lqi;
    link.rssi = neighbour.rssi;
    link.parentAddr = neighbour.short_addr;
  }

  // Nothing carried the parent relationship. With exactly one entry there is
  // still only one thing it can be - an end device has no other neighbour - so
  // report it rather than the link this device plainly has as unknown. Two or
  // more entries and there is a real choice to make, which this cannot make.
  if (!link.valid && link.entries == 1) {
    link.valid = true;
    link.assumed = true;
    link.lqi = only.lqi;
    link.rssi = only.rssi;
    link.parentAddr = only.short_addr;
  }

  esp_zb_lock_release();
  return link;
}
