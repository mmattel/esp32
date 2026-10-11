# Changelog — NanoH2 DS18B20 Zigbee

Entries are in reverse order — newest first.

---

## 3.2.7

**Added:** the boot log says how long `Zigbee.begin()` took: `Zigbee started, rejoining known network (Zigbee.begin() took 290 ms)`. On failure it prints `Zigbee start failed (30012 ms), rebooting`, which is also mirrored. This is visible at Core Debug Level "None" too.

Why: once on 2026-10-11, the first boot after a long time without power (followed by a flash) took about 41 s to join. That fits the Arduino core's 30 s timeout in `Zigbee.begin()` plus a reboot that then joined at once. Two `--wait` cold-boot captures with Core Debug Level "Info" (after a short off time and after 10 minutes) both joined in under a second, so the cause is still open. The new line makes a slow join visible in any capture.

**Confirmed in the field (v3.2.6):** 13 boots without the `vPortExitCritical` crash (before the fix: about 1 in 9). Parent now `0x2CD7`, LQI 173–204, −40 to −46 dBm, from both the neighbour entry and the probe answer.

**No factory reset required, no re-pair, converter unchanged.** Flash only.

---

## 3.2.6

**Fixed: the boot crash, for real.** The v3.2.5 field logs of 2026-10-11 (17:47 to 17:50, nine boots) showed the `vPortExitCritical` assert once more, at the same point. That build registered nothing from `setup()` anymore, so the registration theory of 3.2.1 and 3.2.5 was wrong.

The cause: `zbSetReporting()` calls `esp_zb_zcl_update_reporting_info()` for the text attributes of EP 14 and EP 16 **without the Zigbee lock**. It runs from `applyReporting()` on every join, while the stack's task is busy with that very join, so two tasks are inside the stack at once. The Arduino core's own `setClusterReporting()` takes the lock for exactly this call. The timing fits every crash log: `applyReporting()` runs before `Zigbee connected` is printed, and all three crash logs show `Zigbee started`, then the assert, and never `Zigbee connected`. arduino-esp32 #10879 describes the same assert from the same kind of bug in the core's `scanNetworks()` (fixed in PR #11792).

The bug dates from **3.1.1**, when `setTextReporting()` was introduced. It was only seen from 3.2.0 on because nearly every boot was captured from then on: the crash heals itself with a reboot within a second. A full audit of the stack calls in both sketches found no other unlocked one. The host test now checks the lock.

**Confirmed in the same logs:** LQI and RSSI come from the parent's answer frame, e.g. `frame LQI 183, RSSI -44 dBm`. These are the same values the neighbour table gave at the early joins, so the inversion holds.

**Also seen:** one boot ran into the 30 s `Zigbee.begin()` timeout (`Zigbee failed to start, rebooting`) and then joined through a **different parent, `0x2CD7`**. That was the ninth reset within about three minutes, so the usual parent may simply have been busy with the rapid rejoins.

**No factory reset required, no re-pair, converter unchanged.** Flash only.

---

## 3.2.5

From the v3.2.4 field logs of 2026-10-11, 17:25 to 17:33 (seven boots):

**LQI works.** Every boot with an empty neighbour entry caught the parent's answer frame: LQI 204, the same each time, plausible for a parent 50–60 cm away.

**RSSI now computed from the LQI.** The radio driver's "recent" values were unusable in all five captures: LQI 7–11 at −101 to −121 dBm, noise floor rather than a frame, so 3.2.4's check rightly withheld them. But the stack's LQI is itself computed from the RSSI, by `esp_zb_rssi_to_lqi()` (read from its disassembly): 0 below −80 dBm, 255 from −30 dBm up, `(RSSI + 80) × 255 / 50` in between. One dB is 5.1 LQI steps, so the inverse is exact (`rssiFromLqi()`). It matches all five LQI/RSSI pairs the neighbour table delivered today. LQI 204 is −40 dBm. The radio-driver reads are gone.

**The crash came back once** (17:30:42): the same `vPortExitCritical` assert in the stack's task, right after `Zigbee started`, followed by a reboot. So the unlocked registration removed in 3.2.1 wasn't the whole story. Both crashes (3.2.0, 3.2.4) happened in builds that registered a handler from `setup()` right after `Zigbee.begin()`; 3.2.1–3.2.3, without such a registration, ran seven boots clean. `linkBegin()` now only schedules the registration (`esp_zb_scheduler_alarm()`, under the lock), and the stack's own task performs it. The boot flash (3.2.0) is unchanged, so only one factor changes. If the crash comes back, it's the next suspect.

**No factory reset required, no re-pair, converter unchanged.** Flash only.

---

## 3.2.4

**Fixed: wrong LQI/RSSI from 3.2.3.** The field log of 2026-10-11, 17:16 published `LQI 10, RSSI -91 dBm` for a parent 50–60 cm away, which earlier measured about LQI 170–200 and −40 to −48 dBm. 3.2.3 read the radio driver's "most recent frame" in the ZDO answer callback, ~237 ms after the request. By then the radio had picked up other frames, likely broadcasts from a distant router.

Now a handler in front of the stack's APS data indications (`linkBegin()` in `zb_link`) catches the parent's answer frame itself, sender = parent and ZDO `IEEE_addr_rsp`, as it arrives:

- **LQI:** from the indication, the stack's own value for that frame.
- **RSSI:** the indication has none. The radio driver's values are read in the same moment and used only if its LQI equals the frame's. Otherwise the console says `RSSI not verified` and no RSSI is published.
- **The core keeps working:** every frame is passed on to the Arduino core's own handler, `zb_apsde_data_indication_handler()`, which keeps its binding bookkeeping.
- **Locked:** the handler is registered under the Zigbee lock, after `Zigbee.begin()`.

The diagnostic line now shows the frame's LQI next to the radio's, and whether they match:

```
link probe: parent 0x380B answered after 237 ms (ZDP status 0x00); frame LQI 196, radio LQI 196 RSSI -41 (matches); entry LQI 0 RSSI 127
```

The 3.2.3 log was the seventh boot in a row without the crash fixed in 3.2.1.

**No factory reset required, no re-pair, converter unchanged.** Flash only.

---

## 3.2.3

**Fixed: LQI/RSSI after a reboot, for real this time.** The v3.2.2 diagnostics (field log of 2026-10-11, 17:09) showed the third case: the parent answers every request (ZDP status `0x00`, after ~231 ms), but the stack leaves its neighbour entry at LQI 0 / RSSI +127. The neighbour table simply isn't a source of link quality on this device.

The answer callback now reads the radio driver's LQI and RSSI of the most recently received frame (`esp_ieee802154_get_recent_lqi()` / `_rssi()`) the moment the answer arrives, which is the parent's frame. That value is published. The neighbour entry stays as a fallback. The diagnostic line now shows both, until this join's first value is published:

```
link probe: parent 0x380B answered after 231 ms (ZDP status 0x00); frame LQI 196 RSSI -41; entry LQI 0 RSSI 127 age 112
```

The v3.2.2 log was also the sixth boot in a row without the crash fixed in 3.2.1.

**No factory reset required, no re-pair, converter unchanged.** Flash only.

---

## 3.2.2

**Diagnostic build.** In the v3.2.1 field logs of 2026-10-11 (16:43 to 16:59, five boots), LQI/RSSI still never appeared: `no measurement in it yet` for 14 minutes, although the parent was asked for a frame every 10 s. There was no crash in any of the five boots, and force push averaged 59.9 s.

To tell the three possible causes apart (request not sent, not answered, or answered but the stack doesn't measure that kind of frame), the console now prints a line after each read that followed a request, while there is no value yet and once when the first one arrives:

```
link probe: parent 0x380B answered after 23 ms (ZDP status 0x00); entry LQI 0 RSSI 127 age 0
link probe: parent 0x380B, no answer within 1000 ms; entry LQI 0 RSSI 127 age 0
```

`zb_link` records the answer's arrival, status and delay (`probeAnswer()`), and `readParentLink()` returns the parent entry's raw LQI, RSSI and age even while they count as unmeasured.

**No factory reset required, no re-pair, converter unchanged.** Flash only.

---

## 3.2.1

From the field logs of 2026-10-11, 16:14 and 16:19 (v3.2.0):

**Fixed: crash right after the stack started.** One boot in three ended in `assert failed: vPortExitCritical port.c:618 (port_uxCriticalNesting[0] > 0)` inside the Zigbee stack's task, right after `Zigbee started, rejoining known network`, followed by a reboot. The next statement in `setup()` was `zbReportBegin()`, which registered the stack's send status callback from the main task **without the Zigbee lock**, while the stack task was already running. That registration is gone. The boot flash (3.2.0) ended at the same moment and remains a second suspect; it touches nothing in the stack, but one crash can't clear it. Since the race doesn't show on every boot, this needs several `--reset` captures to confirm.

**Removed: delivery counting.** The send status callback never fired for a single report (`34 sent, 0 delivered, 0 not delivered`). The APS-level confirm handler would hand the application the confirm handling for every frame, so it's no alternative. The summary now reads `reports, last 300 s: N sent, M refused`.

**Fixed: LQI/RSSI missing after a reboot.** `parent 0x380B found, no measurement in it yet` for 8 minutes. The measurement only comes from frames heard from the parent, and since 3.1.2 the coordinator no longer sends Default Responses back. Before each link read, the device now sends its parent a ZDO IEEE address request (`probeParent()` in `zb_link`) and reads 1 s later. That's one hop and never through the coordinator: once per `LINK_INTERVAL_S`, and at most every 10 s while waiting for a first measurement.

**Fixed: force push came every 65 s instead of 60.** The check runs on each reading (~10.8 s), and the next push was timed from the late one. It is now timed from when it was due, so the average is the set interval. A timer more than one interval behind restarts from now instead of catching up.

**Added:** the boot log lists the settings a coordinator can write, with their values, right after the endpoint list.

**No factory reset required, no re-pair, converter unchanged.** Flash only.

---

## 3.2.0

**Added: boot flash.** From power-on until the LED shows a network state, it now flashes cyan fast (100 ms on, 100 ms off). Before, it stayed dark for that time. That can be long: up to `SERIAL_WAIT_MS` (2 s) waiting for a USB host that isn't there, then up to 30 s in `Zigbee.begin()` while the stack rejoins, e.g. after a long power-off. A dark LED for that long looked like a dead board.

`setup()` is blocked during that time, so `loop()` can't drive the LED. A small FreeRTOS task (priority 1) does it instead, started as the first thing in `setup()`. The first `updateLed()` stops it and waits until it has finished, so the task and the main program never drive the LED at the same time. `haltFatal()` stops it too before flashing red.

Minor version: a new feature, no change to the endpoint list or to what the coordinator sees.

**No factory reset required, no re-pair, converter unchanged.** Flash only.

---

## 3.1.7

**Confirmed in the field (v3.1.6, 2026-10-11 15:51):** the console mirror text arrives in Z2M. No `ESP_ERR_INVALID_SIZE` refusal after the join, force push every minute, and a setting written from Z2M showed up on the mirror.

**Changed:** a setting written from Zigbee names the setting again, without its unit, plus the endpoint: `Force push interval EP17: 1 -> 2` (was `EP 17 set to 2, was 1`), or `Temperature correction EP15: -5.00, asked -20.00` when the write was rounded or clamped. The widest realistic case is 48 characters, exactly the mirror's limit. Only a request far outside a setting's range is longer, and the console notes that cut.

**Cleaned up:**

- Comments in both sketches no longer explain the join publish, the settings retry and the heartbeats with bindings. Since every report is unicast, bindings play no part.
- `ds18b20_bus.h`: a comment line ended in `\`, which made the compiler warn about a multi-line comment.
- The WaterFlow README listed the default partition scheme and "USB CDC On Boot: Disabled". The firmware stops at boot on the default scheme, and its own header says Enabled. The table now matches the DS18B20 sketch's.

**No factory reset required, no re-pair, converter unchanged.** Flash only.

---

## 3.1.6

**Changed:** the slot and settings lines no longer get cut by the console mirror (48 characters). Most of them used to be.

- **Slot lines show the last four digits of the ROM code**, e.g. `slot 0 (ABCD): read failed, last 21.4 C`. The full code is in the boot log for every slot. Affected: power-on default, read failed (`never read since boot` is now `never read yet`, `last good` is now `last`), configured but missing, released. The line for a sensor ignored because every slot is taken keeps the full code, since it has no slot.
- **A setting written from Zigbee is named by endpoint:** `EP 15 set to -0.50 (asked -0.44)` when the write was rounded or clamped, `EP 10 set to 300, was 60` when it went through as sent. It used to be `<setting name> from Zigbee, was …`, up to about 70 characters. The name stays as it is, because it's also the endpoint's description, which the coordinator reads.

**Tests:** `logevent_len` understands `mirror-len: %s<=N` for inserted text of known width, so these lines are now checked instead of only listed. `zb_setting` checks both wordings, and that the longest possible one (WaterFlow's 7-digit total, clamped) is 44 characters.

**No factory reset required, no re-pair, converter unchanged.** Flash only. Anything matching the old wording of these lines, such as an automation on the mirror text, needs updating.

---

## 3.1.5

**Added:** two checks so that lines too long for the console mirror (48 characters) are found early, instead of arriving silently cut at the coordinator.

- **At run time:** `ZbMirror::mirror()` prints `Console mirror (EP 14): line above cut to 48 of N characters` on the console, right under any line it has to cut. This catches lines put together at run time: a ROM code, a setting's name, a value.
- **Before flashing:** a new host test, `test/logevent_len`, reads the sources of both sketches and estimates the longest line every `logEvent("…")` format can produce. A fixed wording that can exceed the limit fails `make`. A line proven in code (the slot summary's `static_assert`) is marked `mirror-len: checked in code`.

**Changed:** the scan found that the button-stuck line was always cut, even before the inserted reason. It is now `Button 9 pressed at boot, ignored until idle` or `Button 9 stuck pressed, ignored until idle`.

**No factory reset required, no re-pair, converter unchanged.** Flash only.

---

## 3.1.4

**Fixed:** the console mirror text (EP 14) was still refused with `ESP_ERR_INVALID_SIZE` at 56 characters (field log of 2026-10-11, 15:12).

**Root cause:** the 3.1.3 limit was an estimate from the radio frame layout, and it was too generous. The real check is in the SDK's `esp_zb_zcl_report_attr_cmd_req()`, read from its disassembly: for a cluster that cannot be fragmented, it allows 82 bytes, subtracts the ZCL header (3) and a fixed reserve of 25, and refuses the report if the attribute's size plus its ID and type (3) does not fit what is left. That leaves 51 bytes for a character string including its length byte, so at most 50 characters. All three field results fit this: 64 refused, 56 refused, the 5-character version string accepted.

**Correction to 3.1.1 and 3.1.2:** both explained the missing text with a reporting entry the stack supposedly needed (3.1.1 added `setTextReporting()`, 3.1.2 moved `applyReporting()` before the first mirrored line). The same disassembly shows that `esp_zb_zcl_report_attr_cmd_req()` never looks up a reporting entry. The size was the cause all along. Both changes are harmless and stay.

**Fix:**

- `MIRROR_TEXT_LEN` 56 → 48, two characters of margin.
- A `static_assert` in `zb_mirror.cpp` refuses to compile above 50.
- Shorter wording for the lines that went over 48: the slot summary is now `3 slots: 3 on bus, 0 missing, 0 never seen` (42 characters). Four button lines were shortened, e.g. `Button: release now to factory reset`.

**No factory reset required, no re-pair, converter unchanged.** Flash only.

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
