# CLAUDE.md — ESP32 Zigbee Sketches (Project Root)

This file covers project-wide facts and conventions that apply to both sketches.
Sketch-specific rules live in each sketch's own `CLAUDE.md`.

---

## Two Sketches, Shared Repository

| Sketch | Directory | Current version |
| --- | --- | --- |
| NanoH2 DS18B20 temperature sensor | `NanoH2_DS18B20_Zigbee/` | see `config.h` |
| NanoH2 Hall-effect water flow sensor | `NanoH2_WaterFlow_Zigbee/` | see `config.h` |

Both target the **M5Stack NanoH2** (SKU C149), an ESP32-H2 board with a Grove port,
on-board WS2812 RGB LED, and one pushbutton (G9).

---

## Build and Flash Environment

**The Arduino IDE runs on the same machine as this Claude session** (since
2026-10-11; earlier sessions used a separate build machine). It is Arduino IDE 2 installed
as the flatpak `cc.arduino.IDE2`. Its bundled `arduino-cli` can be called directly to
compile:

```sh
CLI=/var/lib/flatpak/app/cc.arduino.IDE2/current/active/files/arduino-ide/resources/app/lib/backend/resources/arduino-cli
$CLI --config-file ~/.arduinoIDE/arduino-cli.yaml compile \
  -b m5stack:esp32:m5stack_nano_h2:ZigbeeMode=ed,PartitionScheme=zigbee \
  --build-path <scratch-dir>/build-<sketch> NanoH2_DS18B20_Zigbee
```

| Item | Value |
| --- | --- |
| Core | `m5stack:esp32` 3.3.10 (in `~/.arduino15/packages/m5stack/`). Updated from 3.3.9 on 2026-10-16: same Zigbee library and byte-identical SDK libraries, only the upload command in `platform.txt` changed |
| Board | `m5stack:esp32:m5stack_nano_h2` |
| Required options | `ZigbeeMode=ed`, `PartitionScheme=zigbee` (Zigbee 4MB with spiffs) |
| IDE config | `~/.arduinoIDE/arduino-cli.yaml` |
| Sketchbook | This repo. `directories.user` is a flatpak portal path (`/run/user/1000/doc/…/esp32`) that resolves to `/opt/git/esp32` |
| Libraries used | Only core libraries (`Zigbee`, `Preferences`). No third-party library |

**No third-party library is used, and none should be added for 1-Wire.** The usual
OneWire library (2.3.8, the latest release, also in `libraries/`) has not been updated
for the newer ESP32 variants and does not compile for the ESP32-H2. Its direct-GPIO
layer handles the ESP32-C3 as a special case and sends every other ESP32 down the
classic-ESP32 register path, where the registers it uses do not exist on the H2.
DallasTemperature depends on OneWire, so it is out too. The DS18B20 sketch therefore
has its own bit-banged 1-Wire master and DS18B20 driver in
`NanoH2_DS18B20_Zigbee/ds18b20_bus.cpp/.h`. Host tests for it are in `test/onewire`,
which checks the CRC against OneWire's reference table and the ROM search against a
simulated bus. The header comment of `ds18b20_bus.h` has the full reasoning and the
patch a fork of OneWire would need. Fixes to 1-Wire behaviour go into `ds18b20_bus`,
not into a library.

**Upstream tracking:** [OneWire PR #165](https://github.com/PaulStoffregen/OneWire/pull/165)
("Update OneWire_direct_gpio.h (ESP32H2 and ESP32C2)") adds the H2 and C2 to the
direct-GPIO layer and supersedes #160, which added only the H2. It was still open and
unmerged on 2026-10-11. Even once it is merged, a new OneWire release still has to ship.
At that point, switching back to OneWire/DallasTemperature becomes an option, though not
a requirement: `ds18b20_bus` works and has tests.

The `libraries/` folder (OneWire, DallasTemperature, Adafruit_NeoPixel) and
`~/Arduino/libraries` are on the library search path, but neither Zigbee sketch
includes anything from them.

**Consequences:**
- Compile both affected sketches after every firmware change. Report the result as
  it actually came out, including errors. Warnings from the core's own headers
  (`spimem_flash_ll.h` missing-initializer) are expected and can be ignored.
- Use `--build-path` in the scratchpad so the build does not write into the repo or
  clash with the IDE's own build directory.
- Keep the working tree in a buildable state at all times. The user may compile and
  flash from the IDE at any moment.
- Flashing and the serial monitor depend on the board being plugged in (`/dev/ttyACM*`).
  If no port is present, ask the user to flash from the IDE and paste the serial output.

**Field captures.** The device lives outside on its real parent router, and there is
no WLAN there. Taking it inside changes its routing, so Claude and the board are
never available together. The user flashes from the IDE outside, runs
`tools/serial-capture.sh --reset` (timestamped log in `~/nanoh2-logs/`) and comes
back. Claude then reads the newest log there. Prefer `--reset`: it starts the log
with the boot banner, which proves the link and names the build. One capture
without it (2026-10-11 14:13) recorded nothing in 5 minutes, although force push
was on, and the cause was never found. The script refuses a port another program
holds (the IDE's Serial Monitor) and prints a status line every 60 s while the
device is silent.

**The flashed build often lags the repo.** A pasted serial log may come from a build
older than the current working tree. Always read the banner first — it ends in
`v<FW_VERSION>` — and check the `EP <n> -> ...` lines to identify which build
produced the log. Say plainly if the build looks older than the sources before
diagnosing anything. Before adding something the user requests, grep the tree: it may
already be there, uncommitted and unflashed.

---

## Shared Source Files

Several files are **physically duplicated** across both sketches (no shared directory).
Any change to shared logic must be applied to **both** copies:

| Files | Notes |
| --- | --- |
| `zb_mirror.cpp` / `zb_mirror.h` | Console mirror endpoint |
| `zb_version.cpp` / `zb_version.h` | Firmware version endpoint |
| `zb_link_endpoint.cpp` / `zb_link_endpoint.h` | Link quality endpoint base |
| `zb_link.cpp` / `zb_link.h` | LQI/RSSI measurement |
| `zb_report.cpp` / `zb_report.h` | Every attribute report (`zbReport()`), reporting entries, refusal tracking and summary |
| `zb_setting.cpp` / `zb_setting.h` | Writable settings endpoints |

After finishing the DS18B20 side of a change, always check whether the WaterFlow
copies of the same files differ, then port the change and bump WaterFlow's version
independently.

---

## Zigbee Report Delivery — Unicast Only

**Do not use binding-table address mode for `report*()` calls.**
`ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT` (binding-table mode) silently
discards all attribute reports on this hardware even when the binding table is
correctly populated (confirmed in v3.0.x / v2.0.x; related to esp-zigbee-sdk #909).

**Every report goes through `zbReport()` in `zb_report.cpp`** (since DS18B20 v3.1.2 /
WaterFlow v2.1.2). It sends direct unicast to the coordinator:
```cpp
cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;
cmd.zcl_basic_cmd.dst_endpoint = 1;
cmd.dis_default_resp = 1;   // no ZCL Default Response back through the coordinator
```

Never call the core's `reportAnalogInput()`, `reportAnalogOutput()` or
`reportTemperature()` helpers directly: they use binding-table mode. The endpoint
classes hide them with versions that call `zbReport()`, and `ZbSetting::publish()`
calls it directly. The `zb_setting` host test stub has no `reportAnalogOutput()`, so
a call to it that creeps back in fails to compile.

**Default responses stay off.** With `dis_default_resp = 0`, Z2M answers every
report with a Default Response sent through the coordinator and back over the mesh.
On this network the path from the coordinator back to the device's parent is weak,
and that extra traffic contributes to the coordinator SRSP timeouts (see the
DS18B20 README, "Coordinator Throttling").

**Refusals are visible on the console, deliveries are not.** `zbReport()` records
whether the stack took each report, prints one line when an attribute starts being
refused and one when it recovers, and `zbReportHandle()` prints a summary every
`REPORT_STATS_INTERVAL_S` ("N sent, M refused"). Do **not** register
`esp_zb_zcl_command_send_status_handler` (v3.1.2 to v3.2.0 did): it never fired for
a report.
`esp_zb_aps_data_confirm_handler_register()` is no alternative: per the SDK, the
application then has to handle every APSDE confirm itself.

**Every call into the Zigbee stack from `loop()` or `setup()` takes the Zigbee lock**
(`esp_zb_lock_acquire` / `_release`), registrations included, once
`Zigbee.begin()` has run. The stack's task is running by then, and ZBOSS is not
thread-safe.

**LQI/RSSI need incoming frames, and come from the parent's answer frame.** With default
responses off, the device may hear nothing from its parent for minutes.
`probeParent()` (`zb_link`) sends the parent a ZDO IEEE address request before
each link read. It's one hop and never goes through the coordinator. The stack
does **not** update the parent's neighbour entry from those answers: it stays at
LQI 0 / RSSI +127, as seen on 2026-10-11. So a handler in front of the APS data
indications catches the parent's `IEEE_addr_rsp` and takes the frame's LQI, then
calls the Arduino core's `zb_apsde_data_indication_handler()` so the core works as
before. The RSSI is computed back from the LQI (`rssiFromLqi()`, the exact inverse
of the stack's `esp_zb_rssi_to_lqi()`). Don't use
`esp_ieee802154_get_recent_rssi()`: it returned noise-floor values, not frames.
Don't rely on the neighbour table for link quality on this device.

**Missing locks crash the stack's task.** The `vPortExitCritical` assert
(`port_uxCriticalNesting[0] > 0`) right after `Zigbee started`, about one boot in
nine on 2026-10-11, was `zbSetReporting()` calling
`esp_zb_zcl_update_reporting_info()` **without the lock** from `applyReporting()` on
every join (since v3.1.1, fixed in v3.2.6). The core's own `setClusterReporting()`
takes the lock for exactly that call. arduino-esp32 #10879 is the same assert from
the same kind of bug in the core's `scanNetworks()`, fixed in PR #11792. The crash
heals itself with a reboot within a second, so it only shows in a capture: count
boots, not builds. When this assert shows up again, look for an unlocked `esp_zb_*`
call first.

**"Zigbee connected" doesn't mean the parent answered.** On a reboot with stored
network data, the core sets `connected` as soon as the stack signals `Device
rebooted` (core debug output), without any exchange with the parent. The first
real proof of a working link is the parent's answer to `probeParent()`, or a
report that arrives in Z2M.

**Slow first join after a long power-off: open.** Once (2026-10-11), the first
boot after a long time without power, followed by a flash, took ~41 s:
`Zigbee.begin()` ran into the core's 30 s timeout (`ZB_BEGIN_TIMEOUT_DEFAULT`),
the sketch rebooted, and the next boot joined at once. Cold boots after 10 min
off were fast. The boot log line `Zigbee started, … (Zigbee.begin() took N ms)`
shows it. To diagnose, flash with **Core Debug Level = Info**, which makes the
core print its commissioning steps (`Device rebooted`, `Commissioning failed,
trying again…`), and capture with `tools/serial-capture.sh --wait`. Don't change
the timeout or the reboot logic until such a capture exists. Also seen once,
unexplained: with the notebook attached but the port not yet opened, the stack
only started 8.6 s after power-on instead of ~1.6 s. Probably the console output
stalls while nobody reads the USB port.

**Handlers are registered from the stack's own task.** `linkBegin()` schedules the
indication handler's registration with `esp_zb_scheduler_alarm()` (under the lock),
and the stack's task runs it. The core registers its own indication handler once,
in `Zigbee.begin()`, so ours has to come after that.

**A report carries at most 50 characters of string.** `esp_zb_zcl_report_attr_cmd_req()`
refuses a bigger one with `ESP_ERR_INVALID_SIZE`. The rule, read from the SDK's
disassembly on 2026-10-11: for a cluster that can't be fragmented (Analog Input
can't), 82 bytes, minus the 3-byte ZCL header, minus a fixed 25, minus attribute ID
and type (3), leaves 51 bytes for the string including its length byte. This is why
the mirror text never arrived up to v3.1.3: it was padded to 64, then 56.
`MIRROR_TEXT_LEN` is now 48, `zb_mirror.cpp` has a `static_assert` at 50, and every
`logEvent()` line has to fit 48. See Host Tests, `logevent_len`. Any new string
attribute that is reported is under the same limit.

**The reporting entry for the text attributes is not what makes them work.**
`setTextReporting()` / `zbSetReporting()` register attribute 0xF000 in the stack's
reporting table, on the theory from v3.1.1 that `reportText()` needs one. The same
disassembly shows that `esp_zb_zcl_report_attr_cmd_req()` never looks up a
reporting entry. It reads the attribute, checks the size and sends. The registration
is harmless and stays, but don't explain a failing report with a missing entry: the
console now names the actual error. `applyReporting()` still runs before the first
`logEvent()` after a join.

Any new endpoint that reports must do so through `zbReport()`.

---

## Hardware Facts

**Button on G2 (external, Grove yellow) — active LOW.**
Measured 2026-09-15: open = 2.55 V, pressed = 0 V. The contact closes to GND; an
external pull-up holds the pin high while open. `BUTTON_ACTIVE_HIGH` is therefore
**0**. A pin that reads the active level while the internal pull cannot move it is
more likely an opposing external pull resistor than a short — check the actual
voltage before concluding "conductive path". 2.55 V (just above VIH = 2.48 V at
3.3 V VDD) is a voltage divider, not a wire.

**On-board button on G9** (the default `PIN_BUTTON`) is also active LOW and has no
external components — the pin's internal pull-up idles it high, a press pulls it to
GND via the PCB trace.

---

## Zigbee Network

The user's coordinator runs Zigbee2MQTT with these identifiers (confirmed
2026-09-16 / 2026-10-06):

| Item | Value |
| --- | --- |
| PAN ID | `0x1A62` |
| Channel | `11` |
| NanoH2 DS18B20 IEEE | `0x1051dbfffe684d0b` |
| Z2M device name | `NanoH2-Pool` |
| Parent router | Terrasse/Solarpumpe, short address `0x2CD7` (LQI 173–229, −35 to −46 dBm, 2026-10-11/13). Until 2026-10-11 it was Terrasse/Pool, `0x380B`; the device switched during a series of rapid resets and stayed |

These are Z2M's stock defaults — a neighbour's untouched Z2M would look identical in
a scan. When reading `scan: N network(s) in range` output, treat `0x1A62 | 11` as
the user's coordinator. The IEEE address is what to grep for in Z2M logs.
`joining open: no` means Permit join is not asserted, not that the device is refused.

---

## Zigbee2MQTT — External Converters

Each sketch ships a `.mjs` external converter that **exists in two copies**:

- `NanoH2_DS18B20_Zigbee/nanoh2-ds18b20.mjs` — kept with the sketch
- `<z2m-data>/external_converters/nanoh2-ds18b20.mjs` — loaded by Z2M
- Same pattern for `nanoh2-waterflow.mjs`

Every converter change must be applied to both copies. There is no automatic sync.

**Getting a new writable endpoint visible in Home Assistant** (applies after every
re-pair with a new endpoint):
1. Restart Z2M after updating the `.mjs` (or Settings → Reload external converters).
2. Re-interview the device on the Z2M device page.
3. Click the refresh icon on the new endpoint in Z2M.

Step 3 is what actually unblocks HA: a writable entity stays unavailable until HA
receives at least one value report, and the refresh forces that first read.

---

## Core/SDK Assumptions to Re-check After a Core Update

Several fixes rely on **undocumented internals** of the Arduino core
(m5stack:esp32 3.3.10; checked against 3.3.9, unchanged) and the Espressif Zigbee SDK it bundles. They were read from
the core's sources or from disassembly of the SDK libraries
(`riscv32-esp-elf-objdump` on `~/.arduino15/packages/m5stack/tools/esp32h2-libs/<ver>/lib/*.a`).
After a core or board-package update, go through this list:

| Assumption | Where it comes from | Used by | If it no longer holds |
| --- | --- | --- | --- |
| `zb_apsde_data_indication_handler()` is not `static` | core `ZigbeeCore.cpp`, no header | `zb_link.cpp` calls it after its own indication handler | link error: loud, nothing silent |
| the core registers its APS indication handler only once, in `Zigbee.begin()` | core `ZigbeeCore.cpp` | `linkBegin()` registers ours afterwards | ours is replaced, and LQI/RSSI stop updating after a rejoin |
| the stack's LQI is `(RSSI + 80) × 255 / 50`, clipped at −80 / −30 dBm | disassembly of `esp_zb_rssi_to_lqi()` (libzboss_port) | `rssiFromLqi()` | **checked at every boot** by `linkCheckLqiFormula()`: on a mismatch `link: LQI formula changed …`, and no RSSI is published |
| a report carries at most 50 characters of string (82 − 3 − 25 − 3 bytes) | disassembly of `esp_zb_zcl_report_attr_cmd_req()` | `MIRROR_TEXT_LEN` 48, `static_assert` at 50 | visible: `refused … ESP_ERR_INVALID_SIZE` on the console |
| `esp_zb_zcl_report_attr_cmd_req()` looks up no reporting entry | same disassembly | — (`setTextReporting()` is kept, harmless) | nothing |
| the stack doesn't update the parent's neighbour entry from received frames | field observation, 2026-10-11 | `probeFrame()` instead of the table | harmless: the table is the fallback |
| the core sets `connected` on `Device rebooted`, without contact with the parent | core `ZigbeeCore.cpp` | reading of the boot log | affects interpretation only |
| `esp_zb_zcl_command_send_status_handler` doesn't fire for ZCL reports | field observation | not used | — |
| `ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC` is 0xFFFF here and 0x0000 in the next SDK generation | SDK headers | `zbReport()`, by name | nothing, as long as the name is used |
| `ZigbeeEP::reportClusterAttribute` and the core's report helpers use binding-table mode | core | `zbReport()` instead | — |
| the core's `setClusterReporting()` / `scanNetworks()` take the Zigbee lock | core | — | arduino-esp32 #10879 shows what a missing lock looks like |
| `tools/serial-capture.sh` uses esptool **v5** syntax (`--after hard-reset`) and `arduino-cli monitor --timestamp` | IDE flatpak's tools | `--reset`, the capture | `--reset` fails with esptool's error message |

---

## Known Upstream Issues

| Issue | URL | What it affects |
| --- | --- | --- |
| arduino-esp32 #12917 | https://github.com/espressif/arduino-esp32/issues/12917 | Uninitialised `manuf_code` in core report helpers; workaround in `ZbMirror::reportText()` |
| arduino-esp32 #10879 | https://github.com/espressif/arduino-esp32/issues/10879 | Resolved (PR #11792): the same `vPortExitCritical` assert as ours, from an unlocked stack call in the core. A reference for what a missing lock looks like, not a workaround in this code |
| esp-zigbee-sdk #909 | https://github.com/espressif/esp-zigbee-sdk/issues/909 | Silent discard of binding-table-mode reports; fixed by unicast (see above) |
| OneWire PR #165 | https://github.com/PaulStoffregen/OneWire/pull/165 | ESP32-H2/C2 support in OneWire; until it is merged and released, `ds18b20_bus` replaces the library |
| zigbee2mqtt #33407 | https://github.com/Koenkk/zigbee2mqtt/issues/33407 | Opened by the user 2026-10-16: a re-interview doesn't re-send HA discovery, so HA's device info keeps the old firmware until Z2M is restarted. Both READMEs, "Firmware Version", describe the restart; drop it there once a Z2M release fixes this |

Check these periodically; a resolved issue may allow removing a workaround.

---

## Host Tests

Pure-logic units have host tests in `test/`. Run with `cd test && make` after
touching `ds18b20_bus`, `slots`, `zb_setting`, `zb_mirror`, `zb_link`,
`zb_version`, or `zb_report`, and after adding or rewording any `logEvent()` line in
either sketch: `logevent_len` checks that every mirrored line fits
`MIRROR_TEXT_LEN` (48 characters, which the Zigbee stack caps at 50). No board
required.
