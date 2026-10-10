# Changelog — NanoH2 DS18B20 Zigbee

Entries are in reverse order — newest first.

---

## 3.1.1

**Fixed:** `console_mirror` (EP 14, attribute 0xF000) and `firmware_version` text (EP 16, attribute 0xF000) never auto-updated in Z2M — pressing the refresh button returned the correct value, but no reports arrived automatically. The sequence counter (`mirror_line_count`) did update, so the reporting path was partially working.

**Root cause:** `esp_zb_zcl_report_attr_cmd_req()` (called by `reportClusterAttribute()`) looks up a reporting-configuration entry in the SDK's internal table for the attribute before sending. `setAnalogInputReporting()` creates such an entry for `presentValue` (float); no equivalent existed for attribute 0xF000 (char-string). Float attributes therefore reported successfully after v3.1.0's unicast fix; char-string attributes still failed silently on every call.

**Fix:** a new `setTextReporting()` method on `ZbMirror` and `ZbVersion` calls `esp_zb_zcl_update_reporting_info()` directly for attribute 0xF000, creating the required entry. It is called from `applyReporting()` (which already runs on each join, after `Zigbee.begin()`).

**Note on converter v1.6:** the `reporting:` entries added to `m.text()` in converter v1.6 are harmless but do not by themselves fix this — Z2M does not send `configure_reporting` commands for custom text attributes even when `reporting:` is present.

**No factory reset required.** Reflash and rejoin.

---

## 3.1.0

**Fixed:** automatic updates (temperature, console mirror, LQI, RSSI, firmware version) never reached Z2M or HA despite bindings being correctly populated in the device's binding table. Manually pressing the refresh button in Z2M returned values normally; automatic reports never arrived.

**Root cause:** `esp_zb_zcl_report_attr_cmd_req()` with `ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT` (binding-table addressing mode) silently fails on this device even when bindings are present. This was confirmed in v3.0.2: after adding all the `reporting:` entries to the converter, bindings appeared in Z2M's bind tab, but reports still did not arrive. Temperature held for two minutes with a 10-second reading interval produced no updates.

**Fix:** all attribute report calls now use direct unicast to the coordinator at short address 0x0000 endpoint 1 (`ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT`). The binding table is bypassed entirely. Affected: `ZbMirror::reportText()`, `ZbMirror::reportAnalogInput()`, `ZbVersion::reportText()`, `ZbVersion::reportAnalogInput()`, `LinkAnalog::reportAnalogInput()`, `TempEndpoint::reportTemperature()` — each is now a direct unicast, either changed in place (for the existing manual builds) or overriding the inherited library method.

**No factory reset required.** Reflash the firmware. Z2M bindings are no longer needed for reports to reach the coordinator, but re-interviewing is harmless and keeps the bind tab consistent.

**Converter unchanged** — still 1.5. The `reporting:` entries in the converter are now only needed for Z2M's `configure_reporting` step (which tells the device the coordinator's thresholds), not for the bindings. The unicast fix makes the binding irrelevant to delivery.

---

## 3.0.2

**Fixed:** console mirror, LQI, RSSI, and firmware version did not update automatically in Z2M — only after manually clicking the refresh button. The same root cause as the temperature fix in 3.0.1: `reporting:` was absent from the `genAnalogInput` endpoints in the converter, so Z2M never created bindings for those clusters. Every `reportAnalogInput()` and `reportText()` call in the firmware was silently discarded.

The single "Console mirror: cannot be sent" message in the serial log (printed once at join time, then quiet) was the only visible sign of this failure. The message fires when the first report fails at join time before Z2M's configure step has run. Without a binding, subsequent calls also fail — but silently, because the internal `_publishWorked` state is already false and no repeat message is printed.

Converter updated to 1.5: added `reporting:` entries to EP 12 (LQI), EP 13 (RSSI), EP 14 (mirror counter), and EP 16 (firmware version number). Z2M now creates `genAnalogInput` bindings for all four during the configure step.

**No reflash required.** Update the converter in Z2M `external_converters/` and re-interview the device.

---

## 3.0.1

**Fixed:** temperature readings did not appear in Z2M or HA automatically after a factory reset and re-pair. Manually pressing the refresh button in Z2M for each sensor did return a reading, but automatic updates (force push, delta-triggered, heartbeat) never arrived.

**Root cause:** converter 1.3 had `reporting: false` on `m.temperature()`. Z2M interprets this as "skip both configure_reporting and the bind command" for the `msTemperatureMeasurement` cluster. Without a binding in the device's binding table, every `reportTemperature()` call in the firmware was silently discarded by the Zigbee stack. Because previous firmware versions were updated via rejoin (no factory reset), the old binding from the original pair survived in NVS. The v3.0.0 factory reset wiped it; the configure step with `reporting: false` did not rebuild it.

Converter updated to 1.4: replaced `reporting: false` with an explicit reporting config (`{min: 10, max: 3600, change: 25}`, where `change: 25` = 0.25 °C in ZCL units of 0.01 °C). Z2M now creates the binding during the configure step, and the firmware's own deadband and force push continue to control when `reportTemperature()` is called.

**No reflash required.** Update the converter in Z2M `external_converters/` and re-interview the device — the configure step will create the missing binding without a factory reset.

---

## 3.0.0

**Added:** Force Push interval setting (EP 17).

A new writable Analog Output endpoint lets you tell the device to re-publish all
temperatures every N minutes regardless of the reporting delta. Range is 0–120 minutes
in 1-minute steps; 0 disables the feature; default is 60 minutes. The value is stored
in NVS and survives a reboot.

This is a complement to the existing `TEMP_REPORT_HEARTBEAT_S` heartbeat, which repeats
the *last published* value (already inside the deadband). Force push does a fresh read
and bypasses the deadband, so the coordinator receives a current measurement even when
temperatures have not moved enough to trigger normal reporting.

**Why it was added:** overnight temperature drops did not propagate to Z2M because the
values sat inside the reporting delta. Force push gives a configurable periodic
"check-in" that is independent of physical change.

**User action required:** factory reset and re-pair (new endpoint added). Update both
copies of the converter: sketch folder and Z2M `external_converters/`.

---

## 2.0.4

**Fixed:** firmware version and settings took up to 60 s to appear in Z2M after a
join or reflash.

The initial `publish()` calls in `onZigbeeConnected()` fire before Z2M has finished
its configure step, so they arrive before bindings are in place and are not received.
The first `SETTING_REPORT_HEARTBEAT_S` tick (60 s) was therefore the earliest the
values could appear. A one-shot retry now fires `SETTING_REPORT_JOIN_RETRY_MS`
(20 s, `config.h`) after the join — long enough for Z2M's configure step to settle —
and republishes all settings and the firmware version. Normal 60 s heartbeats follow.

**Note:** Z2M's "Firmware build ID" on the device info page is read from the Basic
cluster during the initial interview and is not updated by a reflash or rejoin. It
stays at the previous version until a manual re-interview. The `firmware_version`
expose (endpoint 16) is the authoritative live value and is what this fix accelerates.

No re-pair needed (no endpoint or expose changes).

---

## 2.0.3

**Fixed:** post-join ZCL burst could saturate the Z-Stack coordinator.

`onZigbeeConnected()` sent all pending endpoint reports back-to-back (settings ×3,
firmware version, slot summary — roughly five in under 1 ms), which together with the
flag-driven temperature and link reports that follow in the next loop passes could
saturate the Z-Stack coordinator's ZNP serial queue.  When saturated, the coordinator
could not answer availability pings from other devices, causing Z2M to mark unrelated
devices — range extenders first, then anything routing through them — as offline.

`POST_JOIN_REPORT_DELAY_MS` (150 ms, in `config.h`) is now inserted between each
explicit `publish()` call in `onZigbeeConnected()`, spreading the burst over ~750 ms.

A matching `advanced: adapter_concurrent: 1 / adapter_delay: 100` in Z2M's
`configuration.yaml` provides defence-in-depth at the coordinator side; see
"Coordinator Throttling" in README.md.

No re-pair needed — timing change only; no endpoint or expose changes.

---

## 2.0.2 — converter only, no reflash required

**Fixed:** three expose names in `nanoh2-ds18b20.mjs` contained parentheses
(`reading_interval_(s)`, `reporting_delta_(c)`, `temperature_correction_(c)`).
Home Assistant rejects MQTT discovery topics that contain `(` or `)`, so those three
entities were never registered in HA. Parentheses replaced with underscores:
`reading_interval_s`, `reporting_delta_c`, `temperature_correction_c`.

Update both copies of the converter (sketch folder and Z2M `external_converters/`),
then re-interview the device in Z2M so the corrected entity names are published.
No firmware change; the ESP32 does not need to be reflashed.

---

## 2.0.1

Initial stable release. Firmware and converter shipped together and work as designed:
three DS18B20 temperature sensors, configurable reading interval and reporting delta,
temperature correction per device, LQI/RSSI link quality, console mirror, and firmware
version endpoint. No known issues at release.
