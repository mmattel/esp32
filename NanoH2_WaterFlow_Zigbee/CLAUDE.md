# CLAUDE.md — NanoH2 WaterFlow Zigbee Sketch

This file captures rules and conventions for this project so they don't have to be
re-established each session. Read it before making changes.

---

## Semantic Versioning

The version is three defines at the top of `config.h`:

```c
#define FW_VERSION_MAJOR 2
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
- `README.md` — every place the version appears as a literal. Search for the old version
  string to find them all.

Bump the version in the same commit as the change it names, not in a separate commit.

---

## Endpoint Numbering

Endpoint numbers are fixed in `config.h`. **An endpoint number that moves renames the
expose on every coordinator that already knows this device.**

Current layout:

| Range | Use |
| --- | --- |
| 10–12 | Writable settings (Analog Output): impulses per litre, writeback time, total start value |
| 13–16 | Read-only diagnostics (Analog Input): LQI, RSSI, console mirror, firmware version |
| 20–24 | Flow measurements (Analog Input): L/min, L/s, total L, total m³, total since reset |

Rules:
- New endpoints go at the end of the appropriate block, never inserted between existing
  ones. The gap between 16 and 20 is intentional: room for future diagnostics without
  moving the measurement block.
- `ZB_MODEL` (`"NanoH2-WaterFlow"`) must never contain a version. It is the product
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

- Adding or removing a flow measurement or setting endpoint
- Toggling `ZB_LQI_ENDPOINT`, `ZB_RSSI_ENDPOINT`, `ZB_MIRROR_ENDPOINT`, or
  `ZB_VERSION_ENDPOINT`

This is a major-version change. After re-pairing, regenerate the converter body from
Z2M (device page → Dev console → Generate external definition), then manually restore
the two `m.text()` entries for EP 15 (console mirror) and EP 16 (firmware version) —
Z2M's generator cannot produce text exposes. See the top comment in
`nanoh2-waterflow.mjs` for the full recipe.

---

## External Converter — Two Copies

`nanoh2-waterflow.mjs` lives in this sketch folder **and** in Z2M's `external_converters/`
directory. **Every change must be applied to both copies.** There is no automatic sync;
it is manual.

### Converter Versioning

The converter carries its own version (`// Converter version: X.Y`) and an inline
changelog at the top of `nanoh2-waterflow.mjs`, independent of `FW_VERSION_*`.
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
`reportAnalogInput()` or `reportText()` call is silently discarded — Z2M receives
nothing and the refresh-button-is-required symptom appears.

This was confirmed in the DS18B20 sketch (v3.0.1 and v3.0.2). The old rule "omit
`reporting:` from STATE_GET endpoints" was wrong and has been removed from both
CLAUDE.md files.

Thresholds for read-only diagnostics: `{min: 10, max: 3600, change: 1}`. The firmware
manages its own deadband in software; these are fallbacks only.
Writable settings (`genAnalogOutput`): `{min: 'MIN', max: 'MAX', change: 1}`.

---

## NVS Layout

All keys live under the `nanoh2flow` namespace (`NVS_NAMESPACE` in `config.h`):

| Key | Type | What it stores |
| --- | --- | --- |
| `joined` | bool | Commissioning flag |
| `ipl` | float | Impulses per litre (EP 10) |
| `wbs` | int | Writeback time in seconds (EP 11) |
| `tstart` | float | Last total start value written (EP 12) |
| `total` | float | Running cumulative total in litres (EP 22) |

The running total (`total`) is written on sensor inactivity, not on every sample.
The start value (`tstart`) is written only when EP 12 is written from the coordinator.
A factory reset clears the entire namespace via `Zigbee.factoryReset()` plus an explicit
`prefs.clear()` on `nanoh2flow`.

**Do not add per-sample NVS writes.** Flash endurance is finite; the inactivity-based
writeback pattern exists to keep write cycles proportional to flow events, not to the
1-second sample rate.

---

## README Conventions

- **Headings:** AP title case throughout. Use https://headlinecapitalization.com (select
  AP Style) to check before adding or renaming a section.
- **Line breaks in table cells:** use `<br>`, not `\` or a blank line.
- **TOC:** update `## Contents` when any top-level or second-level section is added,
  removed, or renamed.
- **Version literals:** search and replace all occurrences when bumping.

---

## Build and Test Environment

- **Builds and flashing happen on a separate machine.** Changes travel via git; serial
  output comes back as pasted text. The flashed build may lag the repo — check the
  `EP` lines in the boot banner to identify which commit is running.
- **`LOG_EVERY_SAMPLE 1`** in `config.h` prints a serial line for every 1-second sample,
  including zero-flow intervals. Useful for calibrating `impulses_per_litre`. Default is
  `0` (print only when pulses > 0).

---

## Console and Logging

- Lines that should reach the console mirror **must use `logEvent()`**, not
  `Serial.printf()` directly. `logEvent()` both prints and hands the line to the mirror.
- `Serial.printf()` is fine for output that is debug-only and should not be mirrored
  (e.g. the per-sample flow readings controlled by `LOG_EVERY_SAMPLE`).
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
| esp-zigbee-sdk #909 | https://github.com/espressif/esp-zigbee-sdk/issues/909 | `esp_zigbee_zcl_command.c:263` assert on char-string reports; root cause not yet confirmed. Binding-table report mode silently fails — fixed in v2.1.0 by unicast to 0x0000 ep 1. `reportText()` also requires a reporting-config entry in the SDK's internal table for attr 0xF000; fixed in converter v1.5 by adding `reporting:` to both `m.text()` entries so Z2M sends `configure_reporting` for 0xF000 during re-interview. |
