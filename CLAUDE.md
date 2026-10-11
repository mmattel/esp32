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
| Core | `m5stack:esp32` 3.3.9 (in `~/.arduino15/packages/m5stack/`) |
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

After finishing the DS18B20 side of a change, always check whether the WaterFlow
copies of the same files differ, then port the change and bump WaterFlow's version
independently.

---

## Zigbee Report Delivery — Unicast Only

**Do not use binding-table address mode for `report*()` calls.**
`ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT` (binding-table mode) silently
discards all attribute reports on this hardware even when the binding table is
correctly populated (confirmed in v3.0.x / v2.0.x; related to esp-zigbee-sdk #909).

**All report functions must use direct unicast to coordinator:**
```cpp
cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;
cmd.zcl_basic_cmd.dst_endpoint = 1;
```

Applied in DS18B20 v3.1.0 / WaterFlow v2.1.0 to every report function in the shared
files above and in `TempEndpoint::reportTemperature()`.

**Custom char-string attributes (0xF000) need an extra step:** Z2M never sends
configure_reporting for `m.text()` exposes, so the SDK's internal reporting table has
no entry for attribute 0xF000. Call `esp_zb_zcl_update_reporting_info()` directly
(via `setTextReporting()` on `ZbMirror` and `ZbVersion`) from `applyReporting()` on
each join, otherwise `reportClusterAttribute()` silently does nothing for those
attributes.

Any new endpoint whose firmware calls a `report*()` method must follow the same
unicast pattern. For non-standard attributes, also register the reporting config
manually.

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
| Parent router | Terrasse/Pool (LQI ≈ 176) |

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

## Known Upstream Issues

| Issue | URL | What it affects |
| --- | --- | --- |
| arduino-esp32 #12917 | https://github.com/espressif/arduino-esp32/issues/12917 | Uninitialised `manuf_code` in core report helpers; workaround in `ZbMirror::reportText()` |
| esp-zigbee-sdk #909 | https://github.com/espressif/esp-zigbee-sdk/issues/909 | Silent discard of binding-table-mode reports; fixed by unicast (see above) |
| OneWire PR #165 | https://github.com/PaulStoffregen/OneWire/pull/165 | ESP32-H2/C2 support in OneWire; until it is merged and released, `ds18b20_bus` replaces the library |

Check these periodically; a resolved issue may allow removing a workaround.

---

## Host Tests

Pure-logic units have host tests in `test/`. Run with `cd test && make` after
touching `ds18b20_bus`, `slots`, `zb_setting`, `zb_mirror`, `zb_link`, or
`zb_version`. No board required.
