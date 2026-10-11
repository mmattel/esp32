# Changelog — NanoH2 DS18B20 Zigbee

Entries are in reverse order — newest first.

---

## 3.1.3

**Fixed:** the console mirror text (EP 14) never reached Z2M. This had been true since the mirror existed; the fixes in 3.0.2, 3.1.0 and 3.1.1 each removed a real problem, but not this one.

**Root cause:** the report was too big. The stack refused every text report with `ESP_ERR_INVALID_SIZE`, which became visible with the 3.1.2 diagnostics (field log of 2026-10-11: `EP 14: report of attribute 0xF000 refused by the stack: ESP_ERR_INVALID_SIZE (260)`). Reports are not fragmented, and of the 127 bytes in a radio frame, the headers (both IEEE addresses in the network header, network security, APS) leave about 66 bytes for the ZCL frame. The line is padded to `MIRROR_TEXT_LEN`, so every text report was 3 (ZCL header) + 3 (attribute ID and type) + 1 (length byte) + 64 = 71 bytes, however short the line. The counter and the short firmware version string fit, which is why they arrived.

**Fix:** `MIRROR_TEXT_LEN` 64 → 56, making a text report 63 bytes. Two lines were shortened to fit:

- The slot summary is now `sensors (3 slots): 3 on bus, 0 missing, 0 never seen` (52 characters). It was `temp sensors (3 slots): 3 on the bus, ...` (61).
- `Button: release now to free the missing sensors' slots` (was 60 characters).

Lines built from variable text, such as a setting written from Zigbee, can still exceed 56 and are cut. They were already cut at 64 before.

**Diagnostics:** refusals are now tracked per endpoint **and attribute**. In 3.1.2, the counter's successful report right after a refused text report on the same EP 14 printed a misleading "accepted again".

**No factory reset required, no re-pair, converter unchanged.** Flash only. Z2M trims the trailing spaces as before. Lines are now padded to 56 instead of 64.

---

## 3.1.2

**Context:** with v3.1.1 flashed, the mirror text (EP 14) still never arrived in Z2M while the counter did. Temperatures pushed by force push showed as `published (force push)` on the console but did not show up in Z2M. And the Zigbee network still had spells where Z2M's pings to other routers timed out with `SRSP - AF - dataRequest after 6000ms`, the symptom described in the README's "Coordinator Throttling".

**Fixed:**

- **Join order.** `onZigbeeConnected()` printed "Zigbee connected" through the mirror before `applyReporting()` had created the reporting entry for the text attribute, so the first report of every join was refused. `applyReporting()` now runs first.
- **Settings went through the binding table.** `ZbSetting::publish()` still used the core's `reportAnalogOutput()`, the binding-table mode that silently fails on this device. It now uses unicast like every other report.

**Less traffic through the coordinator:**

- **No Default Response.** Every report is now sent with `dis_default_resp = 1`. Before, the coordinator answered each report with a ZCL Default Response, sent by Z2M as a request of its own through the coordinator and back over the mesh to the device. On this network that path back is the weakest part (coordinator to Terrasse/Pool, LQI 73/79). The APS acknowledgement of each report is unaffected.
- **Settings repeated hourly instead of every minute.** `SETTING_REPORT_HEARTBEAT_S` 60 → 3600. The minute heartbeat cost six reports a minute (four settings plus the version number and text), about 8,600 a day of unchanged values. The join publish and the one-shot retry 20 s after the join are unchanged.

**Diagnostics:**

- **Report outcome on the console.** A new shared `zb_report.cpp/.h` builds every report in one place (`zbReport()`) and registers the stack's send status callback. The console says when an endpoint's reports start being refused by the stack (with the error) or stop being delivered (no APS acknowledgement after retries), and when they recover. That's one line at each end of a spell, not one per report. Every `REPORT_STATS_INTERVAL_S` (300 s) a summary line follows: sent, delivered, not delivered, refused.
- **The temperature line tells the truth.** `published` is printed only when the stack took the report. Otherwise it says `NOT published, report refused` or `not published, offline`.
- **The mirror says which step failed:** setting or reporting, text or counter.
- **Capture script.** `tools/serial-capture.sh` records the serial console with a timestamp per line, so the device can stay on its real parent with the notebook beside it and the log can be lined up with the Z2M log afterwards.

**Tests:** the `zb_mirror` and `zb_version` host tests had stopped compiling with the unicast change in v3.1.0. They now stub `zbReport()`, and a new `zb_report` suite checks the report frame (unicast to 0x0000 ep 1, no default response, no manufacturer code) and the console lines.

**No factory reset required, no re-pair, converter unchanged.** Flash only.

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
