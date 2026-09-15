// Host tests for readParentLink(): picking the parent out of the neighbour
// table, and behaving when there is nothing to pick.
//
// Arduino.h and Zigbee.h in this directory are stubs; the neighbour table is a
// plain array this file fills, and the stack lock is counted so a path that
// returns early cannot quietly leave it held.

#include "Arduino.h"
#include "../../NanoH2_DS18B20_Zigbee/zb_link.cpp"
#include <cassert>

static int fails = 0;
static void check(const char *what, bool ok) {
  printf("  %-42s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) fails++;
}

static esp_zb_nwk_neighbor_info_t table[4];
static uint8_t tableLen = 0;
static int locksHeld = 0;
static int locksTaken = 0;
static bool lockAvailable = true;

esp_err_t esp_zb_nwk_get_next_neighbor(esp_zb_nwk_info_iterator_t *iterator, esp_zb_nwk_neighbor_info_t *nbr_info) {
  if (*iterator >= tableLen) {
    return -1;  // ESP_ERR_NOT_FOUND, "finish iteration"
  }
  *nbr_info = table[(*iterator)++];
  return ESP_OK;
}

bool esp_zb_lock_acquire(TickType_t) {
  if (!lockAvailable) {
    return false;
  }
  locksTaken++;
  locksHeld++;
  return true;
}

void esp_zb_lock_release(void) {
  locksHeld--;
}

static void addNeighbour(uint8_t relationship, uint16_t addr, uint8_t lqi, int8_t rssi) {
  esp_zb_nwk_neighbor_info_t &n = table[tableLen++];
  memset(&n, 0, sizeof(n));
  n.relationship = relationship;
  n.short_addr = addr;
  n.lqi = lqi;
  n.rssi = rssi;
}

int main() {
  printf("no parent to report\n");
  Zigbee.up = false;
  LinkQuality l = readParentLink();
  check("stack not started -> invalid", !l.valid);
  check("stack not started -> no lock taken", locksTaken == 0);

  Zigbee.up = true;
  l = readParentLink();
  check("empty table -> invalid", !l.valid);
  check("empty table -> lock released", locksHeld == 0);

  check("empty table -> no entries counted", l.entries == 0);

  tableLen = 0;
  addNeighbour(ESP_ZB_NWK_RELATIONSHIP_CHILD, 0x1234, 200, -40);
  addNeighbour(ESP_ZB_NWK_RELATIONSHIP_SIBLING, 0x5678, 210, -35);
  l = readParentLink();
  check("two entries, no parent -> invalid", !l.valid);
  check("two entries, no parent -> both counted", l.entries == 2);
  check("no parent entry -> lock released", locksHeld == 0);

  lockAvailable = false;
  l = readParentLink();
  check("lock unavailable -> invalid", !l.valid);
  check("lock unavailable -> nothing held", locksHeld == 0);
  lockAvailable = true;

  printf("parent found\n");
  tableLen = 0;
  // Parent behind other entries: it has to be picked by relationship, not by
  // being the first thing in the table.
  addNeighbour(ESP_ZB_NWK_RELATIONSHIP_CHILD, 0x1234, 200, -40);
  addNeighbour(ESP_ZB_NWK_RELATIONSHIP_PARENT, 0x0000, 168, -62);
  addNeighbour(ESP_ZB_NWK_RELATIONSHIP_OTHERS, 0x9abc, 100, -80);
  l = readParentLink();
  check("valid", l.valid);
  check("lqi is the parent's", l.lqi == 168);
  check("rssi is the parent's, sign kept", l.rssi == -62);
  check("parent short address", l.parentAddr == 0x0000);
  check("flagged, so not assumed", !l.assumed);
  check("whole table counted, not just up to the parent", l.entries == 3);
  check("lock released", locksHeld == 0);

  printf("sole neighbour taken as the parent\n");
  // An end device has one neighbour and it is the parent; some stacks leave the
  // relationship unset, which must not cost the only link measurement there is.
  tableLen = 0;
  addNeighbour(ESP_ZB_NWK_RELATIONSHIP_OTHERS, 0x0000, 140, -70);
  l = readParentLink();
  check("valid", l.valid);
  check("marked as assumed", l.assumed);
  check("lqi from the sole entry", l.lqi == 140);
  check("rssi from the sole entry", l.rssi == -70);
  check("address from the sole entry", l.parentAddr == 0x0000);

  // Two unflagged entries are a choice this cannot make, so it makes none.
  tableLen = 0;
  addNeighbour(ESP_ZB_NWK_RELATIONSHIP_OTHERS, 0x1111, 140, -70);
  addNeighbour(ESP_ZB_NWK_RELATIONSHIP_OTHERS, 0x2222, 150, -60);
  l = readParentLink();
  check("two unflagged entries -> invalid", !l.valid);
  check("two unflagged entries -> not assumed", !l.assumed);

  // A flagged parent is still preferred when it is not the first entry.
  tableLen = 0;
  addNeighbour(ESP_ZB_NWK_RELATIONSHIP_OTHERS, 0x1111, 140, -70);
  addNeighbour(ESP_ZB_NWK_RELATIONSHIP_PARENT, 0x2222, 150, -60);
  l = readParentLink();
  check("flagged parent wins over the first entry", l.parentAddr == 0x2222);
  check("flagged parent is not assumed", !l.assumed);

  printf("edge values pass through\n");
  tableLen = 0;
  addNeighbour(ESP_ZB_NWK_RELATIONSHIP_PARENT, 0xABCD, 255, -128);
  l = readParentLink();
  check("lqi 255 not truncated", l.lqi == 255);
  check("rssi -128 not truncated", l.rssi == -128);
  check("router parent address", l.parentAddr == 0xABCD);

  printf("\n%s\n", fails ? "FAILURES" : "ALL PASS");
  return fails ? 1 : 0;
}
