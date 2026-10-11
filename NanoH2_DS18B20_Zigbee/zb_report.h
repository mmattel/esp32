// Every attribute report this sketch sends, in one place, and what became of it.
//
// One function builds and sends a report, so every endpoint goes out the same way:
//
//   Direct unicast to the coordinator, short address 0x0000, endpoint 1. The
//   binding table mode the core's own helpers use
//   (ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT) silently fails on this device
//   even when the bindings are present - confirmed in DS18B20 v3.0.2 and WaterFlow
//   v2.0.2, esp-zigbee-sdk #909.
//
//   No default response. With the flag clear, the coordinator answers every report
//   with a ZCL Default Response, which Zigbee2MQTT sends as a request of its own
//   through the coordinator's radio and back over the mesh to this device. That
//   doubles the traffic a report costs, and the second half runs from the
//   coordinator, the one node the whole network queues behind. The report is still
//   acknowledged at the APS layer, which is what the delivery tracking below reads.
//
// And what became of each one, which nothing else in the sketch can see: the core
// logs a refused report through log_e(), which Core Debug Level "None" compiles
// away, and whether a report that was accepted ever arrived is only known later,
// when the stack's send status callback fires. zbReportHandle() puts both on the
// console - a line when an endpoint starts failing and another when it recovers,
// not one per report, plus a summary every REPORT_STATS_INTERVAL_S.
//
// Console only, never logEvent(): a failure line handed to the mirror would be a
// report of its own, sent down the same path that just failed.

#pragma once

#include <Arduino.h>
#include "Zigbee.h"

// Sends one attribute of one endpoint's server cluster to the coordinator, as the
// value the attribute holds now - set it first. Returns whether the stack took
// the report; whether it arrived shows on the console later. Does nothing and
// returns false while the stack is not running.
bool zbReport(uint8_t srcEndpoint, uint16_t clusterId, uint16_t attrId);

// Registers the reporting configuration for an attribute in the stack's own table.
// The stack looks one up before it sends a report and refuses one it has no entry
// for. A standard attribute gets its entry from the core's set*Reporting() calls
// or from the coordinator's configure_reporting, but an attribute added by hand -
// the mirror's line, the version string - gets neither. Zigbee2MQTT sends no
// configure_reporting for an m.text() expose. Call after Zigbee.begin(), from
// every join.
//
// No reportable change: these are character strings, a discrete type, and
// discrete types carry none (ZCL 2.6.7.4).
void zbSetReporting(uint8_t endpoint, uint16_t clusterId, uint16_t attrId, uint16_t minInterval, uint16_t maxInterval);

// Hooks into the stack's send status callback. Call once, after Zigbee.begin().
void zbReportBegin();

// Prints whatever changed since the last call. Call from loop().
void zbReportHandle();
