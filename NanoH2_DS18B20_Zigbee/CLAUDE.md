# CLAUDE.md — NanoH2 DS18B20 Zigbee Sketch

This file captures rules and conventions for this project so they don't have to be
re-established each session. Read it before making changes.

---

## Semantic Versioning

The version is three defines at the top of `config.h`:

```c
#define FW_VERSION_MAJOR 2
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 1
```

`FW_VERSION_NUMBER` is derived automatically as `major × 10000 + minor × 100 + patch`,
allowing two digits each for minor and patch (ceiling: 99 each). Never edit
`FW_VERSION_NUMBER` directly.

**What each level means for the user:**

| Level | When to bump | User action required |
| --- | --- | --- |
| Patch | A fix that changes nothing the coordinator sees | Flash + rejoin (no re-pair) |
| Minor | A new feature that leaves the endpoint list and expose names intact | Flash + rejoin (no re-pair) |
| Major | Anything that adds/removes an endpoint, renames an expose, or breaks NVS compatibility | Factory reset + re-pair |

**Files to update on every version bump** (in the same commit as the change):

- `config.h` — the three `FW_VERSION_*` defines
- `README.md` — every place the version appears as a literal (banner example, version
  table, `FW_VERSION_NUMBER` example). Search for the old version string to find them all.

Bump the version in the same commit as the change it names, not in a separate commit.

---

## Endpoint Numbering

Endpoint numbers are fixed in `config.h` (10–16, then 20+ for sensors). **An endpoint
number that moves renames the expose on every coordinator that already knows this device.**

Rules:
- New endpoints go at the end of the fixed block, not next to thematically related ones.
  `EP_CONFIG_CORRECTION` (15) and `EP_VERSION` (16) are examples: both arrived after
  10–13 were in the field and could not be inserted beside their peers.
- `EP_TEMP_BASE` is 20. Temperature slots occupy 20, 21, 22, … up to
  `EP_TEMP_BASE + MAX_DS18B20_SENSORS − 1`.
- The gap between 16 and 20 is intentional: room for future fixed endpoints without
  moving the sensor block.
- `ZB_MODEL` (`"NanoH2-DS18B20"`) must never contain a version. It is the product
  identifier; Z2M keys its device definition on it.

---

## Changing the Endpoint List

**Any of the following requires a factory reset and re-pair, AND the external converter
must be regenerated:**

- Changing `MAX_DS18B20_SENSORS`
- Toggling `ZB_LQI_ENDPOINT`, `ZB_RSSI_ENDPOINT`, `ZB_MIRROR_ENDPOINT`, or
  `ZB_VERSION_ENDPOINT`

This is a major-version change. After re-pairing, regenerate the converter body from
Z2M (device page → Dev console → Generate external definition), then manually restore
the two `m.text()` entries for EP 14 (console mirror) and EP 16 (firmware version) —
Z2M's generator cannot produce text exposes. See the top comment in
`nanoh2-ds18b20.mjs` for the full recipe.

---

## External Converter — Two Copies

`nanoh2-ds18b20.mjs` lives in this sketch folder **and** in Z2M's `external_converters/`
directory. **Every change must be applied to both copies.** There is no automatic sync;
it is manual.

---

## README Conventions

- **Headings:** AP title case throughout. Use https://headlinecapitalization.com (select
  AP Style) to check before adding or renaming a section.
- **Line breaks in table cells:** use `<br>`, not `\` or a blank line.
- **TOC:** update `## Contents` when any top-level or second-level section is added,
  removed, or renamed.
- **Version literals:** see "Semantic Versioning" above — search and replace all
  occurrences when bumping.

---

## Build and Test Environment

- **Builds and flashing happen on a separate machine.** Changes travel via git; serial
  output comes back as pasted text. The flashed build may lag the repo — check the
  `EP` lines in the banner to identify which commit is running.
- **Host tests** for the pure-logic parts live in `../test/`. Run them with
  `cd test && make` after touching `ds18b20_bus`, `slots`, `zb_setting`, `zb_mirror`,
  `zb_link`, or `zb_version`.
- **`MAX_DS18B20_SENSORS 0`** is a test/isolation mode: no temperature endpoints, no
  1-Wire activity, Zigbee joining only. The real default is 3.

---

## Console and Logging

- Lines that should reach the console mirror **must use `logEvent()`**, not
  `Serial.printf()` directly. `logEvent()` both prints and hands the line to the mirror.
- `Serial.printf()` is fine for output that is debug-only and should not be mirrored.
- Every line ends with `CONSOLE_EOL` (defined in `config.h`). Never append `\r\n`
  or rely on the driver to expand `\n`.

---

## Upstream Issues — Check Periodically

These are open issues whose resolution would affect this project. Glance at them when
starting a session that touches the relevant code, or when a workaround that depends on
them feels stale.

| Issue | URL | What it affects |
| --- | --- | --- |
| arduino-esp32 #12917 | https://github.com/espressif/arduino-esp32/issues/12917 | Uninitialised `manuf_code` in core report helpers; workaround is in `ZbMirror::reportText()` |
| esp-zigbee-sdk #909 | https://github.com/espressif/esp-zigbee-sdk/issues/909 | `esp_zigbee_zcl_command.c:263` assert on char-string reports; root cause not yet confirmed |
| OneWire PR #165 | https://github.com/PaulStoffregen/OneWire/pull/165 | OneWire library change; check if merged |
| zigbee2mqtt-frontend #2783 | https://github.com/nurikk/zigbee2mqtt-frontend/issues/2783 | Z2M UI: arrow-button changes not transmitted; already noted in README |
