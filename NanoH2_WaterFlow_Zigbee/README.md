# NanoH2 WaterFlow Zigbee

An Arduino sketch for the [M5Stack NanoH2](https://docs.m5stack.com/en/unit/nanoh2) (ESP32-H2, SKU C149)
that reads a Hall-effect water flow sensor and reports the measurements over Zigbee as a Zigbee End Device (ED).

Measurements go out on five read-only endpoints: **flow rate in [L/min]**, **flow rate in [L/s]**,
**cumulative total in [L]**, **cumulative total in [m³]**, and **a RAM-only total** that resets whenever the start value is written.
Three writable settings let the coordinator tune the calibration constant, control how quickly an idle total is saved
to flash, and set or reset the running counter.

---

## Contents

1. [Components and Photos](#components-and-photos)
2. [Files](#files)
3. [Wiring](#wiring)
   - [Which Button](#which-button)
   - [Supply Voltage](#supply-voltage)
4. [Arduino IDE Settings](#arduino-ide-settings)
5. [LED](#led)
6. [Joining a Network](#joining-a-network)
   - [Pinning the Channel](#pinning-the-channel)
7. [Zigbee Endpoints](#zigbee-endpoints)
8. [Impulses per Litre](#impulses-per-litre)
9. [Running Total and NVS Writeback](#running-total-and-nvs-writeback)
10. [Total Start Value](#total-start-value)
11. [Total Since Last Reset](#total-since-last-reset)
12. [Link Quality and Signal Strength](#link-quality-and-signal-strength)
13. [Console Mirror](#console-mirror)
14. [Firmware Version](#firmware-version)
15. [Serial Console](#serial-console)
    - [Which Build Is Running](#which-build-is-running)
    - [Flashing](#flashing)
16. [Zigbee2MQTT](#zigbee2mqtt)
    - [An Expose That Stays N/A](#an-expose-that-stays-na)
    - [Showing the Mirrored Line](#showing-the-mirrored-line)
    - [Adding the External Converter](#adding-the-external-converter)
    - [When the Endpoint List Changes](#when-the-endpoint-list-changes)
17. [Pushbutton](#pushbutton)
18. [Notes and Limits](#notes-and-limits)

---

## Components and Photos

| Component | Notes |
| --- | --- |
| [M5Stack NanoH2 (C149)](https://de.aliexpress.com/item/1005010653230229.html) | ESP32-H2, Grove HY2.0-4P connector |
| [Hall-effect water flow sensor (e.g. YF-S201)](https://de.aliexpress.com/item/1005003748611606.html) | Open-drain NPN output, 5 V powered |
| [Step-down converter 5 V → 3.3 V](https://de.aliexpress.com/item/1005006018534566.html) | Provides 3.3 V for the LLC's LV side; the Grove port supplies 5 V only |
| [Bidirectional logic-level converter 3.3 V ↔ 5 V](https://de.aliexpress.com/item/1005006765742290.html) | Any standard BSS138-based board |
| [Grove HY2.0 screw-terminal adapter](https://de.aliexpress.com/item/1005007804080645.html) | Breaks out the Grove connector to screw terminals for solid-core sensor wires |
| [AC/DC converter AL0505F (85–270 V AC → 5 V DC, 5 W)](https://de.aliexpress.com/item/1005012352441140.html) | Mains supply for the assembly |
| [USB-C to 2-pin screw-on adapter](https://de.aliexpress.com/item/1005010757932530.html) | Connects the 5 V supply to the NanoH2 |
| [Grove HY2.0-4P cable](https://de.aliexpress.com/item/1005007474983293.html) | Included with NanoH2 |


---

## Files

| File | Purpose |
| --- | --- |
| `NanoH2_WaterFlow_Zigbee.ino` | Main sketch |
| `config.h` | All user-tunable defines: pins, sensor ranges, endpoint numbers, NVS keys |
| `flow_sensor.h` / `flow_sensor.cpp` | Hall-effect pulse counting with ISR and atomic read |
| `zb_setting.h` / `zb_setting.cpp` | Writable Analog Output endpoints with NVS persistence |
| `zb_link.h` / `zb_link.cpp` | Parent-neighbour LQI/RSSI lookup |
| `zb_link_endpoint.h` / `zb_link_endpoint.cpp` | Analog Input endpoints for link values |
| `zb_mirror.h` / `zb_mirror.cpp` | Console mirror endpoint |
| `zb_version.h` / `zb_version.cpp` | Firmware version endpoint |
| `console.h` | `logEvent()` declaration |
| `nanoh2-waterflow.mjs` | Zigbee2MQTT external converter |

---

## Wiring

The NanoH2's Grove port is a HY2.0-4P connector wired as follows:

| Grove wire | Colour | NanoH2 pin | Use |
| --- | --- | --- | --- |
| GND | Black | GND | Common ground |
| 5 V | Red | 5 V | Sensor power |
| G2 | Yellow | GPIO 2 | (unused in this sketch) |
| G1 | White | GPIO 1 (`PIN_FLOW`) | Flow sensor signal via LLC |

The sensor itself is a 5 V device with an open-drain NPN output — the
signal wire swings between 0 V (pulse) and 5 V (idle via an internal
pull-up).  The NanoH2 GPIO is 3.3 V tolerant, so a logic-level converter
(LLC) must sit between the sensor and the board.  The Grove port supplies
5 V only; a step-down converter (5 V → 3.3 V) is therefore needed to feed
the LLC's LV side.  Connect the HV side to Grove 5 V and the sensor's
signal wire.

| From | To |
| --- | --- |
| Grove 5 V (red) | LLC HV+<br>Step-down IN+ |
| Grove GND (black) | LLC GND<br>Step-down IN−<br>Flow sensor GND |
| Step-down OUT+ (3.3 V) | LLC LV+ |
| Step-down OUT− | LLC GND (common) |
| Grove G1 / GPIO 1 (white) | LLC LV signal channel |
| LLC HV signal channel | Flow sensor signal wire |
| Flow sensor VCC | Grove 5 V (red) |

`INPUT_PULLUP` is set on `PIN_FLOW` in firmware so the NanoH2-side of the
LLC already has a 45 kΩ pull-up to 3.3 V; this does not interfere with a
sensor whose pull-up is on the 5 V side of the LLC.

The ISR fires on the `FALLING` edge (`FLOW_PULSE_EDGE`), which is when the
paddle-wheel completes a revolution and the open-drain output pulls the line
low.

### Which Button

`PIN_BUTTON` defaults to `9` — the on-board button — but can be changed to
any available GPIO in `config.h`.  The Grove port's G2 (GPIO 2) is a
convenient external alternative.

One constraint applies to the on-board `G9` only: holding it during power-up
invokes the bootloader and flashes the board instead.  The button must
therefore be pressed on an already-running device.  An external button on
`G2` (or any other pin) has no such restriction.

`BUTTON_ACTIVE_HIGH` is `0`: both the on-board button and any external
button wired contact-to-GND pull the pin low when pressed.  The G2 pin
measures approximately 2.55 V when open and 0 V when shorted to ground,
confirming the active-low wiring.

### Supply Voltage

The NanoH2 runs from USB (5 V) and regulates down to 3.3 V internally.
The flow sensor is powered from the Grove port's 5 V line, which comes
straight from USB.

---

## Arduino IDE Settings

These are the board settings that have been confirmed to produce a working
build.  Other settings are left at their defaults.

| Setting | Value |
| --- | --- |
| Board | ESP32H2 Dev Module |
| USB CDC On Boot | Disabled |
| Flash Size | 4 MB (32 Mb) |
| Partition Scheme | Default 4 MB with spiffs (1.2 MB app / 1.5 MB SPIFFS) |
| Zigbee Mode | **Zigbee ED (End Device)** |

**Zigbee Mode must be set to ED.** The sketch calls `ZIGBEE_MODE_ED` and
will not compile as a coordinator.

---

## LED

The on-board WS2812 RGB LED reports what the device is doing:

| Colour | Meaning |
| --- | --- |
| Magenta (flashing) | Not joined — scanning for a network |
| Green | Joined and connected |
| Yellow | Joined but parent link lost; waiting for the stack to reconnect |
| Red (solid, then fades) | Factory reset armed — hold until white, then release |
| White (brief flash) | Factory reset in progress; device will reboot magenta |

There is no blue state in this sketch: blue was used in the DS18B20 sketch
to signal a sensor-slot release, which has no equivalent here.

---

## Joining a Network

The device scans all channels (11–26 by default) on boot whenever it has
not previously joined a network.  The magenta LED flashes during the scan.

To join:

1. Put the coordinator into join/permit-join mode.
2. Power the NanoH2 (or factory-reset it if it was previously on a different
   network — see [Pushbutton](#pushbutton)).
3. Wait for the LED to turn green.  The serial console prints the assigned
   short address when the join succeeds.

The device is a **non-sleepy** end device (`setRxOnWhenIdle(true)`), which
means it keeps its radio on permanently so the coordinator can reach it for
configuration writes at any time.

### Pinning the Channel

`ZB_CHANNEL` in `config.h` defaults to `0`, which scans all channels.  Set
it to the coordinator's channel number (e.g. `11`) to skip the full scan and
join faster on power-up:

```c
#define ZB_CHANNEL 11
```

Pinning the channel also avoids joining the wrong network when multiple
Zigbee coordinators are reachable.

---

## Zigbee Endpoints

All endpoints use the `genAnalogOutput` (writable) or `genAnalogInput`
(read-only) cluster.  Endpoint numbers are fixed and never change; moving
one would rename the expose on every coordinator that already knows this
device.

| EP | Direction | Z2M expose name | Unit | Range / step | Notes |
| --- | --- | --- | --- | --- | --- |
| 10 | Write | `impulses_per_litre_10` | — | 0 – 5000, step 0.001 | Calibration constant |
| 11 | Write | `nvs_writeback_time_s_11` | s | 1 – 20, step 1 | Inactivity timeout before NVS save |
| 12 | Write | `total_start_value_l_12` | L | 0 – 9 999 999, step 0.001 | Sets and saves running total; resets EP 24 |
| 13 | Read | `parent_link_lqi_13` | — | 0 – 255 | LQI to coordinator/parent |
| 14 | Read | `parent_link_rssi_14` | dBm | −128 – 0 | RSSI to coordinator/parent |
| 15 | Read | `mirror_line_count_15` | — | 0 – 65535 | Console event counter |
| 15 | Read | `console_mirror` | — | text | Last mirrored console line (ext. converter) |
| 16 | Read | `firmware_version_number_16` | — | 0 – 999999 | e.g. 1.0.0 → 10000 |
| 16 | Read | `firmware_version` | — | text | e.g. "1.0.0" (ext. converter) |
| 20 | Read | `flow_rate_l_per_min_20` | L/min | 0 – 9999 | Current flow rate |
| 21 | Read | `flow_rate_l_per_s_21` | L/s | 0 – 999 | Current flow rate |
| 22 | Read | `total_consumption_l_22` | L | 0 – 9 999 999 | Cumulative total (NVS-backed) |
| 23 | Read | `total_consumption_m3_23` | m³ | 0 – 9999.999 | Cumulative total (derived from EP 22) |
| 24 | Read | `total_since_last_reset_l_24` | L | 0 – 9 999 999 | Since last EP 12 write; RAM only |

Endpoints 15 and 16 each carry two exposes: a numeric `presentValue` that
Z2M maps by itself, and a character-string attribute `0xF000` that requires
the external converter.

---

## Impulses per Litre

Hall-effect flow sensors like the YF-S201 generate one voltage pulse per
revolution of their internal paddle wheel.  The number of revolutions per
litre is a sensor-specific constant — the YF-S201 datasheet quotes a
*frequency* relationship (Hz per L/min), not a direct pulse-per-litre figure,
and actual sensors vary from batch to batch.  `FLOW_IMPULSES_PER_L_DEFAULT`
is set to `450`, which is a typical middle-of-range starting point.

To calibrate:

1. Set the impulses-per-litre value to your best estimate and start with
   a known reference volume — a measured jug or a bucket you can weigh.
2. Enable `LOG_EVERY_SAMPLE 1` in `config.h` so the serial console prints
   every interval, including zero-flow intervals.
3. Pass the reference volume through the sensor and note the total the
   device reports.
4. Apply the correction factor: `new_ipl = current_ipl × (reported_L / actual_L)`.
   Write the result to endpoint 10 from the coordinator.
5. Repeat until the reported and actual volumes agree to your satisfaction.

The value is stored in NVS (key `ipl`) so it survives a power cycle.  Endpoint
10 shows the current value and accepts writes at any time; no restart is
needed.

The flow rates (EP 20, EP 21) are computed from the pulses counted in the
last 1-second sample window divided by `impulses_per_litre`.  They are zero
whenever no pulses arrive in a window, so they reflect instantaneous flow
rather than a smoothed average.

---

## Running Total and NVS Writeback

The cumulative total (EP 22, `total_consumption_l`) accumulates in RAM and
is saved to NVS flash — key `total` under the `nanoh2flow` namespace — after
the sensor has been inactive for `nvs_writeback_time_s` seconds (EP 11,
default 5 s).

Inactivity is detected by comparing the current time with `lastActiveSampleMs`,
the timestamp of the last sample in which at least one pulse was counted.
Once that gap exceeds the writeback time the total is saved and the dirty flag
is cleared.  No save happens before any flow has been seen, so a device that
is powered up and left running without water produces no NVS writes.

**Why delayed writeback?**  NVS flash cells have a finite write endurance
(typically 100 000 cycles per sector).  Writing on every 1-second sample
during continuous flow would exhaust that budget in roughly 27 hours of active
flow.  Deferring to post-flow idle makes the write rate proportional to the
number of distinct flow events, not their duration.

On boot, the running total is loaded from the `total` NVS key, not from the
`total start value` setting.  This means the total carries over correctly
across power cycles regardless of what is written in EP 12.

---

## Total Start Value

Writing to endpoint 12 (`total_start_value_l`) does three things atomically:

1. Sets the in-RAM running total to the written value.
2. Saves that value to NVS immediately (no waiting for inactivity).
3. Resets the session total (EP 24, `total_since_last_reset_l`) to zero.

This lets you set the meter to match a physical meter reading, reset the
counter to zero for a fresh start, or carry over a total from a previous
device.

The start value itself is also saved to NVS (key `tstart`) so that Z2M
continues to show what was last written rather than reverting to the default
on the next coordinator interview.  Reading EP 12 back from Z2M will always
return the last value you wrote there.

**Reboots do not re-apply the start value.**  The running total on boot
comes from the `total` key, which may already be larger than `tstart` if
flow has happened since the last write to EP 12.  The start value is only
re-applied by writing to EP 12.

---

## Total Since Last Reset

Endpoint 24 (`total_since_last_reset_l`) counts litres from the last time
EP 12 was written, or from the last reboot — whichever is more recent.  It
lives entirely in RAM and is never saved to NVS; a power cycle always
resets it to zero.

Use it to measure a specific event (filling a tank, watering a garden bed)
without touching the main total: write 0 to EP 12 to zero EP 24, measure
the event, read EP 24.  The running total in EP 22 continues accumulating
undisturbed.

---

## Link Quality and Signal Strength

Endpoints 13 (`parent_link_lqi`) and 14 (`parent_link_rssi`) report the
quality of the link to the parent node — usually the coordinator or a
Zigbee router.  Both are read from the Zigbee neighbour table by looking
up the device's own parent entry; they are the device's view of the link,
not the coordinator's.

| Endpoint | Range | Better end |
| --- | --- | --- |
| LQI (13) | 0 – 255 | 255 |
| RSSI (14) | −128 – 0 dBm | 0 |

A reading is taken every `LINK_INTERVAL_S` seconds (default 300 s) and
published only when the value has changed by more than `LQI_DELTA` (10) or
`RSSI_DELTA` (5 dBm) since the last report, or when the hourly heartbeat fires.

The neighbour table is not always populated immediately after a join.  The
firmware retries up to once per second for `LINK_RETRY_MS` ms if the entry
is not found, and prints a warning on the serial console if it never appears.

---

## Console Mirror

Endpoint 15 carries a copy of the last significant event printed on the
serial console.  Every call to `logEvent()` in the sketch writes the same
line to both the serial port and the mirror endpoint: the counter
(`presentValue`, `mirror_line_count_15`) increments on every new line, and
the text itself goes out in attribute `0xF000`, a ZCL character string
space-padded to `MIRROR_TEXT_LEN` (64) characters.

Lines printed outside `logEvent()` — raw `Serial.printf()` calls for the
per-sample flow readings — go to serial only.  The mirror carries notable
events: joins, setting writes, NVS saves, factory resets, and button events.

The counter is the part Z2M can read by itself (a number in a known cluster).
The text needs the [external converter](#adding-the-external-converter).

**On the limitations:** the mirror holds exactly one line.  A burst of events
(e.g. three settings written in quick succession) shows up as the last one
plus a counter that jumped.  Boot-time lines are never mirrored because the
radio is not up when they are printed.  The serial console is the only complete
record; the mirror is a quick status check from the coordinator without
opening a terminal.

### Showing the Mirrored Line

Endpoint 15 sends two things on every new line: `presentValue`, which Z2M
exposes as `mirror_line_count_15`, and the line itself in attribute `0xF000`
(61440), a ZCL character string.  Z2M handles the first and has nowhere to
put the second — its generated definitions can express a number, not a string.

Read it by hand, no converter required: dev console → endpoint `15` →
`genAnalogInput` → read attribute `61440`.  That works whenever the device is
awake and is the quickest way to answer "what happened".

To have it as an expose, the external converter adds:

```js
m.text({
    name: 'console_mirror',
    cluster: 'genAnalogInput',
    attribute: {ID: 0xf000, type: 0x42},   // 61440, a ZCL character string
    description: 'Last console line worth an event, space padded',
    access: 'STATE_GET',
    endpointName: '15',                    // singular here, unlike m.numeric()
    entityCategory: 'diagnostic',
}),
```

`console_mirror` being free as a name is the point of calling the counter
`mirror_line_count_15` rather than `console_mirror`.  There is no `reporting`
entry because the device reports `0xF000` on its own, on every new line and
on the hourly heartbeat.

---

## Firmware Version

Endpoint 16 carries the firmware version in two places:

- `presentValue` — a sortable integer: 1.0.0 → 10000 (major × 10000 +
  minor × 100 + patch).  Z2M generates `firmware_version_number_16` for this.
- Attribute `0xF000` — the version string "1.0.0".  Needs the external
  converter for `firmware_version`.

The Zigbee Basic cluster's `swBuildId` attribute (0x4000) also carries the
version string, so Z2M shows it on the device info panel regardless of the
converter.

**Versioning rules** (from `config.h`):

| Part | When to bump |
| --- | --- |
| Patch | Fix that changes nothing the coordinator sees |
| Minor | New feature; endpoint list and expose names unchanged |
| Major | Added/removed endpoint, renamed expose, or NVS layout incompatible — requires factory reset and re-pair |

Bump the version in the same commit as the change.

---

## Serial Console

Connect at 115200 baud, 8N1, LF line endings.  The device waits
`SERIAL_WAIT_MS` (2000 ms) after `setup()` starts before the first output
so the terminal has time to connect.

Significant events (`logEvent`) print to the console and are also mirrored
to endpoint 15.  Flow measurements print at every sample interval when
`LOG_EVERY_SAMPLE` is `1`, or only when flow is detected when it is `0`
(the default).

### Which Build Is Running

Every boot prints a header block:

```
NanoH2-WaterFlow v1.0.0 (build 10000)
EP 10 -> Impulses per litre (analog output)
EP 11 -> NVS writeback time (analog output)
EP 12 -> Total start value (analog output)
EP 13 -> Parent link LQI (analog input)
EP 14 -> Parent link RSSI (analog input)
EP 15 -> Console mirror (analog input)
EP 16 -> Firmware version (analog input)
EP 20 -> Flow rate L/min (analog input)
EP 21 -> Flow rate L/s (analog input)
EP 22 -> Total consumption L (analog input)
EP 23 -> Total consumption m3 (analog input)
EP 24 -> Total since reset L (analog input)
```

The `EP <n> -> ...` lines list exactly what the firmware registered with the
Zigbee stack.  Use them to verify that the running build matches the
[external converter](#adding-the-external-converter): if an endpoint in the
converter is not in this list, the expose will stay `N/A`.

### Flashing

The NanoH2 boots into the firmware immediately; there is no separate "hold
BOOT button" step unless the flash is corrupt.  Flash from the Arduino IDE
with the board connected via USB.  The device rejoins the network
automatically after flashing if the network credentials are in NVS —
a factory reset is only needed after a major version bump.

---

## Zigbee2MQTT

### An Expose That Stays N/A

When Z2M has never bound a reporting entry for an endpoint, the expose
shows `N/A` indefinitely regardless of what the device sends.  This is
expected on first pair, because the device publishes its join-time values
before Z2M has finished configuring.  The usual fix is a Z2M **re-interview**
(device page → *Re-interview*), which re-runs configure and sets up the
bindings.  After a re-interview all values should populate within a few
seconds.

If an expose stays `N/A` after re-interview:

1. Check the EP header lines in the serial console to confirm the firmware
   actually registered that endpoint.
2. Check that the external converter lists the endpoint in `m.deviceEndpoints()`
   — an endpoint the converter does not list is hidden regardless.
3. Delete and re-pair the device if the endpoint list changed since the last
   pair (the stack will not automatically discover new endpoints on an
   existing device entry).

### Showing the Mirrored Line

See [Console Mirror → Showing the Mirrored Line](#showing-the-mirrored-line).

### Adding the External Converter

**Prerequisite: external JavaScript must be enabled.**  Z2M silently ignores
converter files while this is off.  Either in `configuration.yaml`:

```yaml
advanced:
  enable_external_js: true
```

or in the frontend under **Settings → Settings → Advanced →
`enable_external.js`**, which has to be checked.  Both are the same switch.

**Then add the file through the UI.**  In the frontend, open **Settings →
External converters**, add a new converter, paste the contents of
[`nanoh2-waterflow.mjs`](nanoh2-waterflow.mjs) and save.  Z2M stores it in
its own `external_converters/` directory and loads it at runtime — no restart
needed, and a syntax error in the file shows up in the log immediately.

> **Tip — what to type as the filename.**  Name it after the *model*, not the
> device: `nanoh2-waterflow.mjs`.  A definition is matched by the `zigbeeModel`
> string the device reports (`ZB_MODEL` in `config.h`), never by its IEEE
> address, so one file covers every board running this sketch.  It must end
> in `.mjs`, and it is a plain name, not a path.

The manual route copies the file into `external_converters/` next to
`configuration.yaml` (Home Assistant add-on: `/config/external_converters/`)
and **restarts** Z2M.  The UI route is preferred because it needs no restart.

Either way, check the result on the device page: **Exposes** should now show
*Console mirror* beside *Mirror line count*, and *Firmware version* beside
*Firmware version number*.  Pressing the on-board button is the quickest
end-to-end test — the event line should appear in `console_mirror`.

A copy of the converter lives with the sketch so that the definition and the
firmware it belongs to stay in one place; Z2M keeps its own copy, so a change
to one must be carried over to the other.

### When the Endpoint List Changes

**An external definition replaces the generated one; Z2M does not merge the
two.**  An endpoint the firmware gained is invisible until the `.mjs` names
it.  This looks like a firmware bug and is not: the boot log lists the
endpoint, the device answers on it, and the device page shows nothing.

Any change to the endpoint list counts:

| Change | What the converter needs |
| --- | --- |
| A new measurement or setting endpoint | Its number in `m.deviceEndpoints()` and an `m.numeric()` for it |
| `ZB_LQI_ENDPOINT` or `ZB_RSSI_ENDPOINT` set to `0` | That entry and its `m.numeric()` removed |
| `ZB_MIRROR_ENDPOINT` set to `0` | Endpoint 15 gone from the list, including the `m.text()` |
| `ZB_VERSION_ENDPOINT` set to `0` | Endpoint 16 gone, both the `m.numeric()` and the `m.text()` |

The order that works:

1. Flash the firmware first and check the `EP <n> -> ...` boot lines.
2. Get Z2M to see the new list: *Re-interview* on the device page, or delete
   and re-pair if the endpoint list grew.
3. Regenerate — device page → *Dev console* → *Generate external definition*
   — and re-add **both** `m.text()` entries (EP 15 and EP 16); they are
   marked in the file.
4. Update **both** copies: the one in Z2M's `external_converters/` and
   [`nanoh2-waterflow.mjs`](nanoh2-waterflow.mjs) here.

---

## Pushbutton

The button does **one** thing: factory reset.  There is no sensor-slot
release (that concept belongs to the DS18B20 sketch).

Everything below holds for whichever button `PIN_BUTTON` points at.  On the
on-board `G9` the hold must happen on a running device, because holding that
pin during power-up puts the chip into flash mode.  An external button on `G2`
has no such restriction.

- **Hold 5 s** (`FACTORY_RESET_HOLD_MS`) **and release** — clears the
  commissioning flag, all three settings, and the running total from the NVS
  namespace, plus the Zigbee stack's network credentials via
  `Zigbee.factoryReset()`.  The device reboots into a factory-fresh state and
  flashes magenta.  The binding table is also cleared; rejoining restores the
  network but not the bindings, so exposes may stay `N/A` until a re-interview
  — see [an expose that stays N/A](#an-expose-that-stays-na).
- **Anything shorter** — nothing happens.

The LED follows the hold: it turns red once the hold passes
`FACTORY_RESET_HINT_MS` (500 ms), then white at `FACTORY_RESET_HOLD_MS` (5 s).
What the LED shows is what letting go now would do: releasing during red
cancels the reset; releasing during white confirms it.

**Both actions fire on the release, not during the hold.**  A pin stuck at the
active level reads pressed and never releases, so it can never trigger a
factory reset, which is the intent: a wiring fault that holds the pin low on
power-up should not erase the network credentials on every boot.

Two guards enforce this:

- **A pin that already reads pressed in `setup()`** is treated as a wiring
  fault and ignored until it goes idle once.
- **A press lasting longer than `BUTTON_STUCK_MS`** (30 s) is treated the same
  way.

Both print a diagnostic to the serial console that includes the measured pin
voltage (on G1–G5, which are ADC-capable on the ESP32-H2) to help separate a
short circuit from a high-impedance pickup.

An inhibited button starts working the moment the pin goes idle
(`Button: idle now, back in use`) — no reboot needed.  If the pin goes idle
only when pressed, the polarity is inverted: flip `BUTTON_ACTIVE_HIGH`.

---

## Notes and Limits

- **The running total is the only value saved to NVS.**  Flow rates and the
  session total (EP 24) live in RAM and are lost on every reboot.  The three
  settings (EP 10–12) are also in NVS but are written only when their value
  changes, not continuously.  This keeps NVS write cycles proportional to
  configuration changes and flow events, not to the sample rate.
- **The impulses-per-litre default (450) is an approximation.**  The YF-S201
  datasheet expresses frequency as a function of flow rate, not a fixed
  pulse-per-litre constant, and the actual value varies across batches.
  Calibrate against a reference volume before using the totals for billing
  or precise measurement — see [Impulses per Litre](#impulses-per-litre).
- **Not a sleepy end device.**  `setRxOnWhenIdle(true)` keeps the radio on so
  the coordinator can write settings at any time.  Appropriate for a USB- or
  mains-powered installation; a battery build would need a different approach.
- **The console mirror holds one line.**  A burst of events shows up as its
  last member plus a counter increment.  Boot-time events are never mirrored
  because the radio is not up when they print.  See [Console Mirror](#console-mirror).
- **The m³ total (EP 23) is derived, not measured.**  It is `total_L / 1000.0`
  computed in firmware on every sample and published alongside EP 22.  There
  is no rounding or independent accumulator; both endpoints track the same
  underlying float.
- **Two open upstream issues touch this sketch.**
  [arduino-esp32#12917](https://github.com/espressif/arduino-esp32/issues/12917):
  seventeen of the core's report helpers leave `manuf_code` uninitialised; this
  is why `ZbMirror::reportText()` builds its report command manually.
  [esp-zigbee-sdk#909](https://github.com/espressif/esp-zigbee-sdk/issues/909)
  asks why a report the stack rejects aborts the device at
  `esp_zigbee_zcl_command.c:263`.  If that abort appears, erase the flash and
  pair fresh before looking anywhere else.

---

This project was co-created with the support of [Claude](https://claude.ai) by Anthropic.
