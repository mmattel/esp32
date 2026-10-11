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

**The Arduino IDE and the board are on a separate machine.** This Claude session runs
on a machine that has no `arduino-cli`, no board, and no serial port. The workflow is:

1. Edit code here → commit → push
2. The other machine pulls → opens in Arduino IDE → compiles → flashes
3. Serial output is copied and pasted back into this conversation

**Consequences:**
- "It compiles" is unverified until the user confirms it. Say so plainly rather than
  implying a clean build.
- Keep the working tree in a buildable state at all times — a `.ino` mid-refactor
  between two turns is a broken build on the other machine.
- Host tests in `test/` are the only code that can be run here (`cd test && make`).
- Compile errors and serial output always arrive as pasted text; ask for it rather
  than guessing.

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

Check these periodically; a resolved issue may allow removing a workaround.

---

## Host Tests

Pure-logic units have host tests in `test/`. Run with `cd test && make` after
touching `ds18b20_bus`, `slots`, `zb_setting`, `zb_mirror`, `zb_link`, or
`zb_version`. No board required.
