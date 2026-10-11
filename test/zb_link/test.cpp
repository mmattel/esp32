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

static int probes = 0;
static esp_zb_zdo_ieee_addr_req_param_t lastProbe;
static bool probeHadCallback = false;
static esp_zb_zdo_ieee_addr_callback_t probeCallback = nullptr;
uint32_t hostMillis = 0;
static esp_zb_apsde_data_indication_callback_t indication = nullptr;
static int indicationLocksHeld = -1;  // locks held while registering
void esp_zb_aps_data_indication_handler_register(esp_zb_apsde_data_indication_callback_t cb) {
  indication = cb;
  indicationLocksHeld = locksHeld;
}
// The Arduino core's own handler, which ours must hand every frame on to.
static int coreHandlerCalls = 0;
bool zb_apsde_data_indication_handler(esp_zb_apsde_data_ind_t) {
  coreHandlerCalls++;
  return false;
}
static esp_zb_apsde_data_ind_t frame(uint16_t src, uint16_t profile, uint16_t cluster, int lqi) {
  esp_zb_apsde_data_ind_t ind = {};
  ind.src_short_addr = src;
  ind.profile_id = profile;
  ind.cluster_id = cluster;
  ind.lqi = lqi;
  return ind;
}
static esp_zb_callback_t scheduled = nullptr;
static int scheduledLocksHeld = -1;
void esp_zb_scheduler_alarm(esp_zb_callback_t cb, uint8_t, uint32_t) {
  scheduled = cb;
  scheduledLocksHeld = locksHeld;
}

// The stack's own conversion, as disassembled from esp_zb_rssi_to_lqi().
static uint8_t stackRssiToLqi(int rssi) {
  if (rssi < -80) return 0;
  if (rssi >= -29) return 255;
  return (uint8_t)((unsigned)(rssi + 80) * 255 / 50);
}
// The stack function the sketch checks itself against at boot. formulaChanged
// stands in for a later SDK that computes LQI some other way.
static bool formulaChanged = false;
extern "C" uint8_t esp_zb_rssi_to_lqi(int8_t rssi) {
  if (formulaChanged) {
    return rssi < -90 ? 0 : rssi > -20 ? 255 : (uint8_t)((rssi + 90) * 255 / 70);
  }
  return stackRssiToLqi(rssi);
}

void esp_zb_zdo_ieee_addr_req(esp_zb_zdo_ieee_addr_req_param_t *cmd_req, esp_zb_zdo_ieee_addr_callback_t user_cb,
                              void *) {
  probes++;
  lastProbe = *cmd_req;
  probeHadCallback = user_cb != nullptr;
  probeCallback = user_cb;
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

  printf("an entry with no measurement in it\n");
  // What the stack holds between creating the parent's entry at the join and hearing
  // a frame from it: LQI 0 with RSSI +127, the int8 maximum for "nothing measured".
  // Reported as no reading, because publishing it would show a dead link at an
  // impossible signal level - and the address is known, so the wait can say whose.
  tableLen = 0;
  addNeighbour(ESP_ZB_NWK_RELATIONSHIP_PARENT, 0x2EC6, 0, 127);
  l = readParentLink();
  check("not valid", !l.valid);
  check("said to be unmeasured", l.unmeasured);
  check("the parent's address is still reported", l.parentAddr == 0x2EC6);
  check("no measurement is passed on", l.lqi == 0 && l.rssi == 0);
  check("lock released", locksHeld == 0);

  // The sole-entry fallback has to make the same distinction: taking an unmeasured
  // entry as the parent would publish the same nonsense with assumed set.
  tableLen = 0;
  addNeighbour(ESP_ZB_NWK_RELATIONSHIP_OTHERS, 0x2EC6, 0, 127);
  l = readParentLink();
  check("sole unmeasured entry -> not valid", !l.valid);
  check("sole unmeasured entry -> not assumed", !l.assumed);
  check("sole unmeasured entry -> unmeasured", l.unmeasured);
  check("sole unmeasured entry -> address kept", l.parentAddr == 0x2EC6);

  // A measured parent behind an unmeasured entry is still found, and a measured one
  // is never called unmeasured.
  tableLen = 0;
  addNeighbour(ESP_ZB_NWK_RELATIONSHIP_OTHERS, 0x1111, 0, 127);
  addNeighbour(ESP_ZB_NWK_RELATIONSHIP_PARENT, 0x2222, 150, -60);
  l = readParentLink();
  check("measured parent found past it", l.valid && l.parentAddr == 0x2222);
  check("and not called unmeasured", !l.unmeasured);
  check("its measurement is the one reported", l.lqi == 150 && l.rssi == -60);

  printf("edge values pass through\n");
  tableLen = 0;
  addNeighbour(ESP_ZB_NWK_RELATIONSHIP_PARENT, 0xABCD, 255, -128);
  l = readParentLink();
  check("lqi 255 not truncated", l.lqi == 255);
  check("rssi -128 not truncated", l.rssi == -128);
  check("router parent address", l.parentAddr == 0xABCD);

  // The two values either end of the measured range, which the guard above must not
  // mistake for the unmeasured entry: 0 dBm is the top of the RSSI endpoint's range,
  // and LQI 0 is a legal worst case rather than a missing measurement.
  tableLen = 0;
  addNeighbour(ESP_ZB_NWK_RELATIONSHIP_PARENT, 0xABCD, 0, 0);
  l = readParentLink();
  check("lqi 0 with rssi 0 is a measurement", l.valid && !l.unmeasured);
  check("and passes through as it is", l.lqi == 0 && l.rssi == 0);

  printf("probing the parent\n");
  {
    int takenBefore = locksTaken;
    probes = 0;
    bool sent = probeParent(0x380B);
    check("handed to the stack", sent && probes == 1);
    check("to the parent, about the parent", lastProbe.dst_nwk_addr == 0x380B && lastProbe.addr_of_interest == 0x380B);
    check("the smallest answer: single device", lastProbe.request_type == 0 && lastProbe.start_index == 0);
    check("with a callback, since a null one is not documented", probeHadCallback);
    check("under the lock, and the lock given back", locksTaken == takenBefore + 1 && locksHeld == 0);
    Zigbee.up = false;
    probes = 0;
    check("nothing while the stack is down", !probeParent(0x380B) && probes == 0);
    Zigbee.up = true;
  }

  printf("the probe's answer\n");
  {
    uint8_t status = 0xFF;
    uint32_t after = 0;
    hostMillis = 1000;
    probeParent(0x380B);
    check("nothing before the answer", !probeAnswer(&status, &after));
    hostMillis = 1023;
    probeCallback((esp_zb_zdp_status_t)0x00, nullptr, nullptr);  // as the stack's task would
    check("answered, with its status and how long it took",
          probeAnswer(&status, &after) && status == 0x00 && after == 23);
    hostMillis = 5000;
    probeParent(0x380B);
    check("a new probe forgets the old answer", !probeAnswer(&status, &after));
    hostMillis = 5100;
    probeCallback((esp_zb_zdp_status_t)0x85, nullptr, nullptr);
    check("a timeout comes back as one", probeAnswer(&status, &after) && status == 0x85 && after == 100);
  }

  printf("RSSI back from the stack's LQI\n");
  {
    bool allExact = true;
    for (int rssi = -80; rssi <= -30; rssi++) {
      bool clipped = true;
      int back = rssiFromLqi(stackRssiToLqi(rssi), &clipped);
      if (back != rssi || (clipped != (rssi == -80 || rssi == -30))) {
        printf("    rssi %d -> lqi %u -> %d%s\n", rssi, stackRssiToLqi(rssi), back, clipped ? " clipped" : "");
        allExact = false;
      }
    }
    check("every RSSI from -80 to -30 dBm comes back exactly, the ends as bounds", allExact);
    bool clipped = false;
    check("the five pairs from the field", rssiFromLqi(183, &clipped) == -44 && rssiFromLqi(209, &clipped) == -39
                                            && rssiFromLqi(163, &clipped) == -48 && rssiFromLqi(173, &clipped) == -46
                                            && rssiFromLqi(188, &clipped) == -43);
    check("LQI 204 is -40 dBm", rssiFromLqi(204, &clipped) == -40 && !clipped);
    check("255 is a bound at -30", rssiFromLqi(255, &clipped) == -30 && clipped);
    check("0 is a bound at -80", rssiFromLqi(0, &clipped) == -80 && clipped);
  }

  printf("the boot-time check of the stack's formula\n");
  {
    int8_t bad = 0;
    check("this SDK's formula is the one expected", linkCheckLqiFormula(&bad));
    formulaChanged = true;
    check("a changed formula is caught", !linkCheckLqiFormula(&bad));
    check("naming an RSSI it no longer gives back", bad >= -81 && bad <= -29);
    formulaChanged = false;
    linkCheckLqiFormula(&bad);  // leave it trusted for the frame tests below
  }

  printf("catching the parent's answer frame\n");
  {
    check("linkBegin() schedules, nothing registered yet", linkBegin() && scheduled != nullptr && indication == nullptr);
    check("scheduled under the Zigbee lock", scheduledLocksHeld == 1 && locksHeld == 0);
    check("not hooked before the stack ran it", !linkHooked());
    scheduled(0);  // the stack's task runs it
    check("the stack's task registers the handler", indication != nullptr && linkHooked());

    ProbeFrame f;
    probeParent(0x380B);
    check("nothing seen before the answer", !probeFrame(&f));
    coreHandlerCalls = 0;
    indication(frame(0x1234, 0x0000, 0x8001, 30));  // someone else's IEEE_addr_rsp
    indication(frame(0x380B, 0x0104, 0x0006, 30));  // the parent, but not the answer
    check("other frames are not the answer", !probeFrame(&f));
    check("but every one goes on to the core", coreHandlerCalls == 2);

    indication(frame(0x380B, 0x0000, 0x8001, 204));
    check("the parent's answer is seen", probeFrame(&f) && f.lqi == 204);
    check("its RSSI from its LQI", f.rssi == -40 && !f.clipped);
    check("trusted, since the formula checked out", f.rssiTrusted);
    {
      int8_t bad = 0;
      formulaChanged = true;
      linkCheckLqiFormula(&bad);
      ProbeFrame g;
      check("not trusted once the check failed", probeFrame(&g) && !g.rssiTrusted && g.lqi == 204);
      formulaChanged = false;
      linkCheckLqiFormula(&bad);
    }
    check("and handed on as well", coreHandlerCalls == 3);
    indication(frame(0x380B, 0x0000, 0x8001, 10));  // a second answer must not overwrite the first
    check("the first answer stays", probeFrame(&f) && f.lqi == 204);

    probeParent(0x380B);
    check("a new probe forgets the old frame", !probeFrame(&f));
  }

  printf("the raw entry, measured or not\n");
  {
    tableLen = 0;
    addNeighbour(ESP_ZB_NWK_RELATIONSHIP_PARENT, 0x380B, 0, 127);
    LinkQuality r = readParentLink();
    check("unmeasured, and the raw fields still filled", r.unmeasured && r.rawLqi == 0 && r.rawRssi == 127);
    tableLen = 0;
    addNeighbour(ESP_ZB_NWK_RELATIONSHIP_PARENT, 0x380B, 180, -45);
    r = readParentLink();
    check("measured: raw and published agree", r.valid && r.rawLqi == 180 && r.rawRssi == -45);
  }

  printf("\n%s\n", fails ? "FAILURES" : "ALL PASS");
  return fails ? 1 : 0;
}
