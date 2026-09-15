#include "zb_link.h"
#include "Zigbee.h"

LinkQuality readParentLink() {
  LinkQuality link = {false, 0, 0, 0};

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
  while (esp_zb_nwk_get_next_neighbor(&it, &neighbour) == ESP_OK) {
    // An end device should have nothing but its parent in there, but the table
    // is the stack's, not ours, so pick the entry by relationship rather than
    // trusting it to hold exactly one.
    if (neighbour.relationship != ESP_ZB_NWK_RELATIONSHIP_PARENT) {
      continue;
    }
    link.valid = true;
    link.lqi = neighbour.lqi;
    link.rssi = neighbour.rssi;
    link.parentAddr = neighbour.short_addr;
    break;
  }

  esp_zb_lock_release();
  return link;
}
