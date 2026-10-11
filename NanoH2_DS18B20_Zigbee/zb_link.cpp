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
  LinkQuality link = {};

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
    link.rawLqi = neighbour.lqi;
    link.rawRssi = neighbour.rssi;
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
    link.rawLqi = only.lqi;
    link.rawRssi = only.rssi;
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

// The answer's content is not needed - its arrival is the point, see zb_link.h - but
// whether it came, with which status and how fast, is what tells the failure cases
// apart. Written by the stack's task, read by loop() through probeAnswer(): single
// writer, plain values, and probeSent is only rewritten while no answer is expected.
static volatile bool probeAnswered = false;
static volatile uint8_t probeStatus = 0;
static volatile uint32_t probeAnsweredMs = 0;
static uint32_t probeSentMs = 0;
static volatile uint16_t probeTarget = 0xFFFF;  // the parent asked; 0xFFFF while nothing is expected
static volatile bool probeFrameSeen = false;
static volatile uint8_t probeFrameLqi = 0;
static volatile bool indicationHooked = false;

static void onProbeAnswer(esp_zb_zdp_status_t status, esp_zb_zdo_ieee_addr_rsp_t *, void *) {
  probeStatus = (uint8_t)status;
  probeAnsweredMs = millis();
  probeAnswered = true;
}

int8_t rssiFromLqi(uint8_t lqi, bool *clipped) {
  *clipped = lqi == 0 || lqi == 255;
  if (lqi == 0) {
    return -80;  // or below
  }
  if (lqi == 255) {
    return -30;  // or above: the formula already reaches 255 at -30
  }
  return (int8_t)(((uint16_t)lqi * 50 + 254) / 255 - 80);  // ceil(lqi * 50 / 255) - 80
}

bool probeFrame(ProbeFrame *out) {
  if (!probeFrameSeen) {
    return false;
  }
  out->lqi = probeFrameLqi;
  out->rssi = rssiFromLqi(out->lqi, &out->clipped);
  return true;
}

// The Arduino core's APS indication handler (ZigbeeCore.cpp). Not static there, and
// declared in no header, so it is declared here to be called after ours: the core
// uses it to refresh its list of bound devices when a bind or unbind request
// arrives, and that has to keep working.
bool zb_apsde_data_indication_handler(esp_zb_apsde_data_ind_t ind);

static const uint16_t ZDO_PROFILE_ID = 0x0000;
static const uint16_t ZDO_IEEE_ADDR_RSP = 0x8001;

// Runs in the stack's task for every APS frame received, before the stack handles it.
static bool onApsIndication(esp_zb_apsde_data_ind_t ind) {
  if (ind.status == 0 && ind.profile_id == ZDO_PROFILE_ID && ind.cluster_id == ZDO_IEEE_ADDR_RSP
      && ind.src_short_addr == probeTarget && !probeFrameSeen) {
    probeFrameLqi = (uint8_t)ind.lqi;
    probeFrameSeen = true;
  }
  return zb_apsde_data_indication_handler(ind);  // false: the stack goes on as usual
}

// Runs in the stack's own task, scheduled by linkBegin(). Registering from there
// rather than from setup() leaves no moment where two tasks are inside the stack.
// (This was introduced in v3.2.5 to fix the vPortExitCritical assert after
// Zigbee.begin(); that turned out to be the unlocked zbSetReporting() - see
// zb_report.cpp - but registering from the stack's task is no less right for it.)
static void registerIndication(uint8_t) {
  esp_zb_aps_data_indication_handler_register(onApsIndication);
  indicationHooked = true;
}

bool linkBegin() {
  if (!Zigbee.started() || !esp_zb_lock_acquire(portMAX_DELAY)) {
    return false;
  }
  // Scheduling is itself a call into the stack, so it takes the lock too.
  esp_zb_scheduler_alarm(registerIndication, 0, 0);
  esp_zb_lock_release();
  return true;
}

bool linkHooked() {
  return indicationHooked;
}

bool probeAnswer(uint8_t *status, uint32_t *afterMs) {
  if (!probeAnswered) {
    return false;
  }
  *status = probeStatus;
  *afterMs = probeAnsweredMs - probeSentMs;
  return true;
}

bool probeParent(uint16_t parentAddr) {
  if (!Zigbee.started()) {
    return false;
  }
  // Under the Zigbee lock like every other call into the stack from loop(): the stack
  // is not thread-safe, and its own task is running.
  if (!esp_zb_lock_acquire(portMAX_DELAY)) {
    return false;
  }
  probeAnswered = false;
  probeFrameSeen = false;  // a frame belongs to the answer of this probe, not an older one
  probeTarget = parentAddr;
  probeSentMs = millis();
  esp_zb_zdo_ieee_addr_req_param_t req = {};
  req.dst_nwk_addr = parentAddr;
  req.addr_of_interest = parentAddr;
  req.request_type = 0;  // single device response: the smallest answer there is
  req.start_index = 0;
  esp_zb_zdo_ieee_addr_req(&req, onProbeAnswer, nullptr);
  esp_zb_lock_release();
  return true;
}
