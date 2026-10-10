# CLAUDE.md — NanoH2 DS18B20 Zigbee Sketch

This file captures rules and conventions for this project so they don't have to be
re-established each session. Read it before making changes.

---

## Semantic Versioning

The version is three defines at the top of `config.h`:

```c
#define FW_VERSION_MAJOR 3
#define FW_VERSION_MINOR 1
#define FW_VERSION_PATCH 0
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
- `changelog/changelog.md` — add a new entry at the top (newest first) describing what
  changed and why.

**Converter-only changes** (fixes to `nanoh2-ds18b20.mjs` that require no firmware
reflash) still get a patch bump and a changelog entry. Update `config.h` and `README.md`
as normal; note clearly in the changelog that no reflash is needed.

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

## New Writable Endpoints — Home Assistant Visibility

After a re-pair with a new `genAnalogOutput` (writable) endpoint, HA does **not**
show the entity automatically. The full procedure, in order:

1. Place the updated `.mjs` in `external_converters/` and **restart Z2M** (or
   Settings → Reload external converters).
2. **Re-interview** the device on the Z2M device page.
3. **Click the refresh icon** on the new endpoint in Z2M.

Step 3 is the one that is easy to miss: HA marks a writable entity as unavailable
until it receives at least one value report, and the refresh forces that first read.
Steps 1 and 2 alone leave the entity absent from HA.

---

## Documenting New Endpoints

**Any endpoint added to the firmware must also be documented in `README.md`:**

1. Add a row to the endpoint table in `## Zigbee Endpoints`.
2. Add (or update) the `## Contents` TOC if a new section is added.
3. Add a dedicated `## <Feature Name>` section explaining what the endpoint does,
   its default, range, step, NVS persistence, and any behavioural notes.
4. Add the endpoint to the external converter and note that both copies must be updated.

A new endpoint that has no README entry is invisible to the user, and an endpoint
that is registered but undocumented will look like a bug.

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

### Converter Versioning

The converter carries its own version (`// Converter version: X.Y`) and an inline
changelog at the top of `nanoh2-ds18b20.mjs`, independent of `FW_VERSION_*`.
Bump the minor on every change and add a one-line entry to the changelog in the same
commit. There is no patch level — converter changes are either visible (exposes change)
or invisible (internal cleanup), and both get a minor bump so the history stays linear.

### Converter authoring rules

**No `m.identify()`** — the device does not implement the identify cluster. Including it
makes Z2M send extra bind and configure_reporting commands during its configure step for
no benefit.

**Every endpoint that the firmware ever calls `report*()` on MUST have a `reporting:`
entry in the converter** — including `STATE_GET` (`genAnalogInput`) ones. Z2M uses
the presence of a `reporting:` entry to also send a bind command to the device. Without
the bind, the device's binding table has no entry for that cluster, and every
`reportAnalogInput()`, `reportTemperature()`, or `reportText()` call is silently
discarded — Z2M receives nothing and the refresh-button-is-required symptom appears.

This was confirmed twice: v3.0.1 fixed `msTemperatureMeasurement` (EP 20–22), v3.0.2
fixed `genAnalogInput` (EP 12, 13, 14, 16). The old rule "omit `reporting:` from
STATE_GET endpoints" was wrong and has been removed.

Use sensible values for the thresholds; the firmware manages its own deadband in
software and these are fallbacks only:
- Read-only diagnostics (LQI, RSSI, mirror counter): `{min: 10, max: 3600, change: 1}`
- Temperature: `{min: 10, max: 3600, change: 25}` (change: 25 = 0.25 °C in ZCL units)
- Firmware version (changes only on reflash): `{min: 10, max: 65534, change: 1}`
- Writable settings (`genAnalogOutput`): `{min: 'MIN', max: 'MAX', change: 1}`

After a factory reset+re-pair, always re-interview the device so Z2M's configure step
sends the bind commands. No re-interview = no bindings = silent report failures.

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
| esp-zigbee-sdk #909 | https://github.com/espressif/esp-zigbee-sdk/issues/909 | `esp_zigbee_zcl_command.c:263` assert on char-string reports; root cause not yet confirmed. Binding-table report mode (`ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT`) silently fails even with valid bindings — fixed in v3.1.0 by switching all reports to direct unicast (0x0000 ep 1). `reportText()` also requires a reporting-config entry in the SDK's internal table for attr 0xF000; fixed in converter v1.6 by adding `reporting:` to both `m.text()` entries so Z2M sends `configure_reporting` for 0xF000 during re-interview. |
| OneWire PR #165 | https://github.com/PaulStoffregen/OneWire/pull/165 | OneWire library change; check if merged |
| zigbee2mqtt-frontend #2783 | https://github.com/nurikk/zigbee2mqtt-frontend/issues/2783 | Z2M UI: arrow-button changes not transmitted; already noted in README |
