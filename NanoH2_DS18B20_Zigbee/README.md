# NanoH2 DS18B20 Zigbee sensor

Three DS18B20 temperature sensors on one 1-Wire bus, published over Zigbee from
an M5Stack NanoH2 (ESP32-H2, SKU C149), with a pushbutton for factory reset and
the on-board RGB LED as a link-state indicator.

Three is only the default; see [Changing the sensor
count](#changing-the-sensor-count).

## Contents

- [Files](#files)
- [Wiring](#wiring)
  - [Which button](#which-button)
  - [Supply voltage — check this before powering up](#supply-voltage--check-this-before-powering-up)
- [Arduino IDE settings](#arduino-ide-settings)
- [LED](#led)
- [Joining a network](#joining-a-network)
- [Zigbee endpoints](#zigbee-endpoints)
- [Changing the sensor count](#changing-the-sensor-count)
- [Reading interval and reporting delta](#reading-interval-and-reporting-delta)
  - [Why the delta moves in quarters](#why-the-delta-moves-in-quarters)
  - [One decimal, everywhere](#one-decimal-everywhere)
  - [How the delta gates reporting](#how-the-delta-gates-reporting)
- [Link quality and signal strength](#link-quality-and-signal-strength)
- [Console mirror](#console-mirror)
- [Serial console](#serial-console)
  - [Telling an empty slot from a sensor that has failed](#telling-an-empty-slot-from-a-sensor-that-has-failed)
  - [Why the first lines used to arrive mangled](#why-the-first-lines-used-to-arrive-mangled)
- [Zigbee2MQTT](#zigbee2mqtt)
  - [An expose that stays N/A](#an-expose-that-stays-na)
  - [Showing the mirrored line](#showing-the-mirrored-line)
  - [Adding the external converter](#adding-the-external-converter)
- [Pushbutton](#pushbutton)
- [Notes and limits](#notes-and-limits)

## Files

| File | Contents |
| --- | --- |
| `NanoH2_DS18B20_Zigbee.ino` | Application: link state machine, LED, sampling, button |
| `config.h` | Every tunable: pins, colours, flash cycle, sensor count, interval, delta, hold time |
| `ds18b20_bus.h/.cpp` | Self-contained 1-Wire master and DS18B20 driver |
| `slots.h/.cpp` | What each slot has read, and the lines that say which of the four states it is in |
| `zb_setting.h/.cpp` | A setting with a code default, an NVS override and a Zigbee override |
| `zb_temp_endpoint.h/.cpp` | Temperature endpoint that also publishes its sensor's ROM code |
| `zb_link.h/.cpp` | LQI and RSSI of the link to the parent, from the neighbour table |
| `zb_link_endpoint.h/.cpp` | Analog input endpoint that can state its unit, for the RSSI in dBm |
| `console.h` | `logEvent()`: prints a line and hands it to the console mirror |
| `zb_mirror.h/.cpp` | The console mirror endpoint: the last line worth an event, as text |
| `nanoh2-ds18b20.mjs` | Not firmware: the Zigbee2MQTT external converter, so the mirrored line becomes an expose |

The pure-logic parts — the 1-Wire driver, the slot bookkeeping, the settings, the link lookup and the
console mirror — have host tests in [`../test/`](../test/); run them with `cd test && make`.

## Wiring

The Grove HY2.0-4P port carries `GND` (black), `5V` (red), `G2` (yellow) and
`G1` (white).

| Signal | Pin | Notes |
| --- | --- | --- |
| DS18B20 data | `G1` (Grove white) | all sensors in parallel, one 4.7 kΩ pull-up to their supply rail |
| Pushbutton | `G9` (on-board) *or* `G2` (Grove yellow) | `G9` is the default and needs no wiring at all; an external one goes with one side to the pin, other side to `GND`, internal pull-up enabled in software — see [Which button](#which-button) |
| RGB LED | `G11` | on-board WS2812 |
| RGB power | `G10` | on-board, must be driven high or the LED stays dark |

With an external button on the Grove port, swap `PIN_ONEWIRE` and `PIN_BUTTON` in
`config.h` if your wiring is the other way round.

> **The cable colours are not reliable.** `G1` = white and `G2` = yellow is what
> the NanoH2 prints next to its Grove port, but the cable you plug in may well be
> the other way round: white on `G2` and yellow on `G1`. Only `5V` = red and
> `GND` = black are dependable. Ring the cable out with a multimeter, or go by the
> serial log. A swapped pair puts the bus on `G2` rather than `G1`, which reads as:
>
> ```
> 1-Wire scan: 0 DS18B20 found
> ```
>
> Point `PIN_ONEWIRE` at `2` in `config.h`. With an external button sharing the port
> the same swap also puts the sensors' 4.7 kΩ pull-up on the button pin and the
> button on the bus, and then the button does nothing at all either, because that
> pull-up holds its pin at the idle level whatever the contact does — swap
> `PIN_ONEWIRE` and `PIN_BUTTON`.

An external button is wired the same way as in the [pushbutton
sketch](../NanoH2_Button_LED/): it pulls the pin down to `GND` when closed, and
the internal pull-up — 45 kΩ typical — holds the pin high while the contact is
open. Nothing but the contact and `GND` is needed; measuring `G2` against `GND`
confirms it before flashing, about 3.3 V with the button open and 0 V with it
pressed. For a long run to the button, add an external 10 kΩ pull-up from `G2` to
3.3 V so induced noise cannot register as a press — a ready-made button breakout
usually has one on board, wired to its `VCC` pin. A button that feeds 3.3 V into
the pin instead needs one line in `config.h`:

```c
#define BUTTON_ACTIVE_HIGH 1
```

That switches the pin to `INPUT_PULLDOWN` and inverts the level test. Take that
3.3 V from a 3V3 pad, not from the Grove 5 V rail — see below.

Both Grove pins are ordinary GPIOs: on the ESP32-H2 the strapping pins are
`GPIO8`, `GPIO9` and `GPIO25` (datasheet Table 3-1), so nothing on `G1` or `G2`
can influence how the chip boots. `G1`…`G5` double as `ADC1_CH0`…`CH4`, which is
what lets the button diagnostic report an actual voltage. (ESP-IDF's per-chip GPIO
table lists `GPIO2` and `GPIO3` as strapping pins for the H2 — that contradicts
the datasheet, and the datasheet is the one to trust here.)

`G9` is in that strapping list, and it is where the on-board button sits — which is
what gives the next section its one caveat.

### Which button

The button can be an external one on the Grove port or the board's own, and
`PIN_BUTTON` in `config.h` is the entire switch. **No code changes.**
`buttonPressed()`, the debounce, the hold-and-release and both stuck-pin guards are
written in terms of `PIN_BUTTON` and `BUTTON_ACTIVE_HIGH`, so every value works the
same way.

| `PIN_BUTTON` | Button | Wiring |
| --- | --- | --- |
| `9` | the on-board button beside the USB-C socket | the default: none |
| `2` | external, Grove yellow | one side to `G2`, other side to `GND` |
| `1` | external, Grove white | needs `PIN_ONEWIRE` moved to `2` — the bus and the button cannot share a pin |

`BUTTON_ACTIVE_HIGH 0` is right for all three: an external contact to `GND` and the
on-board button both pull the pin down when closed, and both idle high on the
internal pull-up. `BUTTON_ACTIVE_HIGH 1` is a question for an external button only —
nothing about the on-board one can be rewired.

What to expect from the default `9`:

- **It is also the flashing button.** `G9` is the boot strapping pin, so holding it
  while the board powers up puts the H2 into ROM download mode and the sketch never
  runs at all: dark LED, silent console, no join. The factory reset therefore has to
  be done on a device that is already up — press, hold, release — and never by
  holding the button across a power cycle. With an external button on `G2` the two
  gestures stay on separate buttons and cannot be confused.
- **The diagnostic loses its voltage line.** `BUTTON_PIN_HAS_ADC` is
  `PIN_BUTTON >= 1 && PIN_BUTTON <= 5` and `GPIO9` is no `ADC1` channel, so it turns
  itself off. What remains is the pull-both-ways probe, which is the more telling
  half anyway; the `probe: … mV` line and the resistance arithmetic below it apply
  to an external button on `G1`…`G5`.
- **The boot guard goes quiet.** A pin that already reads pressed in `setup()` is
  next to impossible on `G9`: if it really were held, the chip would be in download
  mode instead of running this code. The guard stays useful for a damaged or shorted
  switch.
- **One hint in the diagnostic stops applying.** Its last line suggests checking that
  `PIN_BUTTON` and `PIN_ONEWIRE` match the Grove wiring, which is advice for an
  external button.
- **`G2` stays free.** Nothing else in the sketch claims it.

Either choice is invisible to everything else: the endpoint list, the NVS contents
and the Zigbee side do not know which pin the button is on, so switching costs no
re-pair and no factory reset.

### Supply voltage — check this before powering up

The sketch assumes **external (non-parasite) power**: `VDD` wired, `GND` wired,
data pulled up to the same rail as `VDD`.

The Grove connector only provides **5 V**, and that is a problem for a 3.3 V
MCU. The DS18B20 needs `VIH ≥ 0.7 × VDD`, which is 3.5 V at a 5 V supply — more
than the H2 can drive. Run the sensors from **3.3 V** with the pull-up to 3.3 V
as well. Confirm against the [NanoH2
schematic](https://docs.m5stack.com/en/core/NanoH2) whether a 3V3 pad is
exposed; if not, add a small 3.3 V regulator off the Grove 5 V rail. Powering
the sensors from 5 V while pulling up to 3.3 V is out of spec and only appears
to work.

## Arduino IDE settings

Two cores will build this sketch; either needs a 3.x release for the bundled
`Zigbee` library.

| Setting | M5Stack core | Espressif core |
| --- | --- | --- |
| Board Manager URL | [`package_m5stack_index.json`](https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json) | [`package_esp32_index.json`](https://espressif.github.io/arduino-esp32/package_esp32_index.json) |
| Board | **M5Stack → M5NanoH2** | **ESP32H2 Dev Module** |
| Zigbee mode | **Zigbee ED (end device)** | **Zigbee ED (end device)** |
| Partition Scheme | **Zigbee 4MB with spiffs** | **Zigbee 4MB with spiffs** |
| USB CDC On Boot | **Enabled** | **Enabled** |

Prefer `M5NanoH2` — its variant already carries the right clock (96 MHz) and the
board's own pin names. Follow [Arduino Board
Management](https://docs.m5stack.com/en/arduino/arduino_board) to install that
core; note it ships M5Stack boards only, so *ESP32H2 Dev Module* is not in the
same list. The generic board remains a fine fallback, as the sketch names every
pin itself in `config.h` and uses nothing from the variant.

Whichever board you pick, **Zigbee mode** and **Partition Scheme** default to
*Disabled* and *Default 4MB with spiffs*, and both have to be changed by hand.
They fail differently, and neither implies the other:

- Wrong **Zigbee mode** and the sketch does not compile — the `#error` at the top
  of the `.ino` catches it.
- Wrong **Partition Scheme** and it compiles, links and flashes; the two
  partitions the Zigbee stack keeps its network credentials in (`zb_storage`,
  `zb_fct`) are simply not there. The sketch checks for them before
  `Zigbee.begin()`, prints which one is missing and flashes red rather than
  letting the stack abort with `ZB_ESP_NVRAM: Failed to find zb_storage
  partition` in a reboot loop.

**Core Debug Level** may stay at *None*: everything the sketch has to say it
prints itself. Raising it to *Info* adds the Zigbee stack's own commentary, which
is worth having when a join goes wrong — that is the level at which lines like
`Network steering was not successful` and `Device started up in factory-reset
mode` appear.

To enter download mode: hold the on-board `G9` button, *then* plug in USB-C. With
the default `PIN_BUTTON 9` that is the same button the sketch reads — held during
power-up it flashes, held while the sketch runs it factory resets; see [Which
button](#which-button).

## LED

| State | LED |
| --- | --- |
| Never joined a network | magenta, flashing on a 3 s cycle |
| Joined and on the air | solid green |
| Joined before, radio lost | yellow, flashing on a 3 s cycle |
| Factory-reset hold in progress | solid red |
| Factory-reset hold long enough, release to reset | solid white |
| Cannot run at all | red, flashing on a 3 s cycle; reason on the serial console |

Cycle length and duty are `LED_FLASH_CYCLE_MS` / `LED_FLASH_DUTY_PCT`; all
colours are `LedColor` constants at the top of `config.h`.

## Joining a network

A factory-fresh device flashes magenta and looks for a network to join. The stack
retries that — *network steering* — once a second, indefinitely, and it logs the
failures only at **Core Debug Level → Info**; at the default `None` a device that
can find nothing to join looks exactly like one that is not trying at all.

**No button press is involved.** After a fresh flash the device starts steering in
`setup()` and keeps at it until it gets in; all that is needed on the other side is
an open permit-join window. If joining seems to need the button, that is the
pushbutton pin misbehaving rather than the joining logic — see
[Pushbutton](#pushbutton).

So the sketch reports the wait itself, and scans for what is on the air:

```
Zigbee started, waiting to be commissioned
Zigbee: still waiting to be commissioned, 30s so far
scan: 1 network in range
  PAN ID | CH | joining open | room for an end device
  0x1A62 | 15 | no           | yes
  none of them is open - joining has to be enabled on the coordinator
```

| Knob | Default | Meaning |
| --- | --- | --- |
| `JOIN_HINT_INTERVAL_S` | 30 | how often the wait is reported; 0 silences it, scan included |
| `JOIN_SCAN_INTERVAL_S` | 30 | how often to scan; 0 keeps the hint and never scans |
| `JOIN_SCAN_DURATION` | 3 | listening time per channel, 1 (fastest) to 4 (most thorough) |

How to read the scan:

- **Nothing listed** — no Zigbee network was heard on any of the 16 channels. The
  coordinator is off, out of range, or its radio is not running. Nothing in this
  sketch can fix that.
- **`joining open` = no** — the network is there and in range but is not accepting
  new devices, which is the normal state. Open it on the coordinator (Zigbee2MQTT
  *Permit join*, ZHA *Add device*, deCONZ *Add new device*); it typically closes
  again after a few minutes, so the device has to be powered and looking while it
  is open.
- **`room for an end device` = no** — the coordinator or router that answered has
  no free child slot. Joining has to happen through a different one, so move the
  board closer to another router.
- All 16 channels are scanned, and joining is attempted on all of them, so a
  coordinator on an unusual channel is not a reason for a failed join.

The scan shares the single radio with the join attempts, which is why it runs far
less often than the hint, and not at all once joined.

The same hint appears when a device that *has* joined loses its parent, worded
`still looking for its network` — there the LED is yellow, permit-join has
nothing to do with it, and the scan is a range check.

## Zigbee endpoints

With the default of three sensors:

| Endpoint | Cluster | Purpose |
| --- | --- | --- |
| 10 | Analog Output | reading interval, seconds |
| 11 | Analog Output | reporting delta, °C |
| 12 | Analog Input | LQI of the link to the parent, 0 … 255 (read-only) |
| 13 | Analog Input | signal strength of that link, dBm (read-only) |
| 14 | Analog Input + a text attribute | the [console mirror](#console-mirror): how many lines, and the last one (read-only) |
| 20, 21, 22 | Temperature Measurement | one per sensor slot |

An Analog Output cluster carries a single value, and so does an Analog Input one,
so every setting and every measurement needs an endpoint of its own.

What describes the device comes first, what it measures last: the two settings,
the two link measurements, the console mirror, then the temperature slots from
`EP_TEMP_BASE` up. The numbers themselves carry no meaning — any assignment within
1 … 240 is legal — so the order is a readability choice, and the endpoints are
registered in the same order, because that is the order in which the stack reports
them and the order a coordinator lists them in.

The five low ones are fixed constants in `config.h`, deliberately *not* derived
from the sensor count, so changing that count leaves them — and the names a
coordinator derives from their numbers — untouched. The gap between 14 and
`EP_TEMP_BASE` leaves room for further settings.

Each temperature endpoint exposes all three required identifiers:

- **static ID** — the endpoint number (`EP_TEMP_BASE` + slot, so 20 + slot by
  default). The slot ↔ sensor mapping is stored in NVS, so slot 0 keeps meaning
  the same physical sensor across reboots even if bus enumeration order changes.
- **sensor internal ID** — the DS18B20's 64-bit ROM code as 16 hex digits, in
  the Basic cluster's **LocationDescription** attribute (0x0010), e.g.
  `28FF641E1234ABCD`. Unassigned slots read `UNASSIGNED`.
- **temperature** — the measured value, read on the configured interval and
  published when it moves by more than the delta.

Every slot endpoint exists whether or not a sensor is plugged in, because the
endpoint list is fixed at `Zigbee.begin()` and cannot grow later without
re-pairing.

Every endpoint reports the *same* manufacturer and model — `ZB_MANUFACTURER` /
`ZB_MODEL`, `M5Stack` / `NanoH2-DS18B20`. That pair identifies the product, and
coordinators key their device definition on it, so nothing instance-specific
(such as a ROM code) may go in there; that is what LocationDescription is for.

## Changing the sensor count

One line in `config.h`:

```c
#define MAX_DS18B20_SENSORS 3
```

The count is the second line of the boot log, so what a build was compiled with is
visible without reading `config.h`:

```
M5Stack NanoH2 - DS18B20 over Zigbee
Sensor slots: 3
```

A build with none prints `Sensor slots: none configured` there — the word
rather than a `0`, so
it reads as the configuration it is and not as a count that failed to print.

Everything else follows it — the endpoint objects, the temperature endpoint
numbers, the `romN` keys in NVS, the slot arrays and every loop over them. Nothing
else in the sources needs editing, and `static_assert`s in the sketch catch the
ways of getting it wrong: a negative count, a count so large that the temperature
endpoints would run past the Zigbee maximum of 240, and anything above **16**,
which is the ceiling. The bus scan tracks which slots are filled in a 16-bit mask,
so a 17th slot would fall out of it and be reported as permanently unfilled — a
silent wrong answer, which is the one outcome a configuration mistake must not
have. 16 sensors on one bit-banged bus is already well past what a single pull-up
holds, so it is a ceiling on the configuration rather than a limit worth raising:
the `static_assert` in the `.ino` says what to widen if it ever is.

**Zero is a valid count.** With `MAX_DS18B20_SENSORS 0` there are no temperature
endpoints, the settings, the two link endpoints and the mirror stay at 10 … 14, and
`PIN_ONEWIRE` is never driven at all — no bus scan, no conversions, and
`1-Wire bus unused, no slots to map a sensor onto` in place of the scan line. What is left is
the Zigbee side, the two settings, the
[link quality](#link-quality-and-signal-strength), the [console
mirror](#console-mirror), the LED and the button,
which is a useful way to bring up a board before any sensor is wired. The reading interval keeps ticking and finds nothing
to do. (With *Compiler warnings: All* the per-slot loops then warn
`comparison is always false` — they are the loops that must not run; the default
warning level says nothing.)

That is a different thing from **no sensor being plugged in**, which is fine at
any count and needs no configuration: every slot endpoint exists regardless,
reads `UNASSIGNED` until a sensor claims it, and the bus is re-scanned every
`ONEWIRE_RESCAN_INTERVAL_MS` so a sensor connected later is picked up without a
reboot.

What changes on the air:

- **The settings, link and mirror endpoints stay where they are**, at 10 … 14, because
  their numbers are fixed rather than derived from the count. Only the temperature
  endpoints change: a slot is added above the last one or removed from the top, so
  in Zigbee2MQTT the count of `temperature_2x` exposes changes and the rest of the
  names do not.
- **The device has to be re-paired.** The endpoint list is fixed at
  `Zigbee.begin()`. After flashing, hold the button for 5 s and release to factory
  reset, then join again. In Zigbee2MQTT, delete the device first and let it re-interview;
  otherwise Z2M keeps serving the cached definition with the old endpoint set.
- **The external converter has to follow the count.**
  [`nanoh2-ds18b20.mjs`](nanoh2-ds18b20.mjs) lists the temperature endpoints twice —
  in `m.deviceEndpoints()` and in `m.temperature({endpointNames: …})` — and both
  lists have to hold exactly `MAX_DS18B20_SENSORS` endpoints, counting up from
  `EP_TEMP_BASE` (20). One too many gives an expose that can never update and a
  binding Z2M logs as failed during `configure`; one too few hides a sensor that is
  really there. Regenerate rather than edit by hand: see [Adding the external
  converter](#adding-the-external-converter).

Two smaller things:

- **Reducing the count leaves stale `romN` keys** in NVS for the slots that no
  longer exist. They are never read again, and the factory reset that the re-pair
  needs anyway clears them.
- **The 1-Wire side does not care.** All sensors convert in parallel, so the
  sampling time is the same for one sensor as for ten. The practical ceiling is
  the bus itself — total cable length and the single pull-up — not the firmware.

## Reading interval and reporting delta

Two separate knobs: the **interval** is how often the bus is read, the **delta**
is how much a reading has to move before it is published.

| | Interval | Delta |
| --- | --- | --- |
| Code default | `TEMP_INTERVAL_DEFAULT_S` — 60 s | `TEMP_DELTA_DEFAULT_C` — 0.25 °C |
| Range | 10 … 3600 s | 0 … 20 °C |
| Step | 1 s | 0.25 °C |
| Endpoint | 10 | 11 |

Each is printed as precisely as it can be set — the interval in whole seconds, the
delta in quarters — since the step is what rounds a write, so a further digit
could not differ:

```
Reading interval (s): 60 (code default)
Reporting delta (C): 0.25 (code default)
Reporting delta (C) written from Zigbee: 0.44 -> 0.50
```

The written value keeps two decimals on purpose: it is what the coordinator asked
for, and showing it unrounded is what makes the rounding visible.

A write only prints when it moved something: the value in effect, or the number
itself through rounding or clamping. A coordinator that rewrites the value already
in effect stays silent on the console.

Both resolve the same way, each source overriding the one above it:

1. The code default in `config.h`.
2. The value stored in NVS by a previous run.
3. A value written to the analog output endpoint from the coordinator.

A write is rounded to the step, clamped to the range and persisted to NVS. A write
that survives that untouched needs nothing sent back: the stack has already stored
it in the attribute, which is where the coordinator reads it. Only a write this did
not take as sent — rounded or clamped — is mirrored back, so a corrected write shows
up on the coordinator rather than silently diverging, while an accepted one leaves
the coordinator's own number alone. The interval minimum stays above the 750 ms
conversion time of a 12-bit reading. Both live in NVS, so they survive a reboot; a
factory reset restores the code defaults.

Which leaves the question of how a coordinator that did *not* write the value ever
learns it. Nothing else reports these two endpoints — `applyReporting()` covers the
temperature and link endpoints only, and the core's analog output cluster takes no
reporting configuration — and the publish on join is too early to help: a report
only reaches whoever is already bound, and Zigbee2MQTT binds while it interviews.
That is why a freshly joined device showed an empty interval and delta until the
first write. Both are therefore repeated every `SETTING_REPORT_HEARTBEAT_S`
(60 s), which is what fills them in, and what refills them after a coordinator
restart that lost its state.

### Why the delta moves in quarters

An Analog Output `PresentValue` is a single-precision float, and a coordinator
snaps what you type to the step it reads from the endpoint's `Resolution`
attribute. Neither can hold a tenth: with a step of 0.1, asking Zigbee2MQTT for a
delta of 0.7 °C gets you **0.700000010430813** in its UI — that is 7 × 0.1 done in
the coordinator's own arithmetic on the float it read from us, and the device's own
nearest float to 0.7 is 0.699999988 in turn. Neither number is wrong and neither is
0.7.

A quarter of a degree *is* exact in binary, so every settable value — 0.25, 0.5,
0.75, 1.0 … — is the same number on both sides and shows as itself. The cost is
that 0.7 cannot be set at all; 0.75 is the nearest. The interval needs none of
this: whole seconds are exact anyway.

**Coming from a build with a finer step**, a value in NVS that the current build
cannot represent is re-rounded on load *and stored*, so what is in NVS and what is
in use cannot drift apart silently:

```
Reporting delta (C): 0.00 (from NVS)
Reporting delta (C): stored 0.10 does not fit this build's range and step, re-stored as 0.00
```

Watch that first case: the old minimum of 0.1 °C rounds to **0**, which publishes
every reading that moves at all. Set the delta again after upgrading if the second
line names a value you cared about.

### One decimal, everywhere

A reading is rounded to `TEMP_PUBLISH_DECIMALS` — one decimal — the moment it comes
off the bus, before the deadband looks at it:

```
slot 0  EP 20  28FF641E1234ABCD  21.4 C  published
```

That one number is then what the console prints, what the temperature attribute
holds, what is reported and what Zigbee2MQTT shows. Rounding only on the way to the
console would print a value the coordinator never received.

One decimal is the precision the sensor stands behind: a 12-bit DS18B20 resolves
0.0625 °C but is accurate to ±0.5 °C, so the digits below are noise — and it is
finer than the 0.25 °C grid `TEMP_DELTA_STEP_C` puts the deadband on, so the
deadband can still tell two readings apart. Raising the define brings those digits
back; 0 rounds to whole degrees.

### How the delta gates reporting

A reading is published only when it differs from the **last published** value by
**more than** the delta. Within the deadband the temperature attribute is left
untouched, so there is nothing to report; outside it the attribute is written
*and reported explicitly* with `reportTemperature()`.

The explicit report is the important half. Leaving it to the stack's own change
detection would apply the reportable change from the ZCL reporting configuration
on top of our deadband — and the coordinator owns that configuration: Zigbee2MQTT
sets it to 1.00 °C during `configure`, which would quietly turn any delta below
1 °C into 1 °C. An explicit report is not subject to it. For the same reason the
device-side reportable change is fixed at 0 in `applyReporting()` rather than
tracking the delta.

`TEMP_REPORT_HEARTBEAT_S` (1 h by default) repeats the last published value even
while readings sit inside the deadband, so a quiet device still produces traffic;
set it to 0 to disable periodic reports entirely. It is handed to the stack as the
ZCL `max_interval` *and* kept by the sketch itself, for the same reason the delta
is: the coordinator owns the reporting configuration and Zigbee2MQTT overwrites
it, so the number in `config.h` is only what actually happens because the sketch
does not depend on the stack for it. A repeat is printed as `(heartbeat)`.

One consequence worth knowing: reading the attribute directly returns the last
*published* value, not the instantaneous one. By construction it is within the
delta of reality.

Right after a join or a rejoin the deadband is bypassed once per sensor
(`lastPublished` is reset to `NAN`), so the coordinator always starts with real
values instead of waiting for the first threshold crossing.

## Link quality and signal strength

The device measures the link to its **parent** and reports both halves of it, the
LQI on endpoint 12 (`EP_LINK_LQI`) and the signal strength on endpoint 13
(`EP_LINK_RSSI`), with both on the serial console:

```
link: parent 0x0000  LQI 168/255  RSSI -62 dBm  published LQI and RSSI (first)
link: parent 0x0000  LQI 174/255  RSSI -74 dBm  published RSSI
link: parent 0x0000  LQI 174/255  RSSI -74 dBm  published LQI and RSSI (heartbeat)
```

A poll that finds both values inside their deadbands prints nothing — see [Serial
console](#serial-console).

| | Scale | Says |
| --- | --- | --- |
| LQI | 0 … 255, unitless | how cleanly frames from the parent decode |
| RSSI | dBm, roughly −30 … −95 | how strongly they arrive |

They usually move together but not always, and that is the useful part: a strong
signal with a poor LQI means interference rather than distance — something else is
transmitting on the channel — while both dropping together is just range. As a
rule of thumb, −30 to −60 dBm is comfortable, −70 workable, and past −85 the link
starts dropping frames.

Neither is the same number as Z2M's `linkquality`:

| | Measured by | Says |
| --- | --- | --- |
| Z2M `linkquality` | the coordinator or a router | how well the network hears this device |
| endpoints 12 and 13 | this device | how well this device hears its parent |

The two directions are regularly asymmetric — a coordinator with a good antenna
hears a weak device fine — and it is the device-side value that decides whether a
setting write, a read or an OTA ever arrives.

`zb_link.cpp` reads both from the stack's own network neighbour table in one go,
picking the entry whose `relationship` is `ESP_ZB_NWK_RELATIONSHIP_PARENT` (an end
device has just the one). The table belongs to the Zigbee task, so the read takes
`esp_zb_lock_acquire()`, the same lock the Zigbee library takes for every
attribute access.

A table holding exactly one entry that is *not* flagged as the parent is taken as
the parent anyway, since an end device has no other neighbour it could be, and
that is said once per join:

```
link: the one neighbour is not flagged as the parent - reading it as one anyway
```

The relationship is the stack's own bookkeeping, so this costs nothing when it is
right and saves the only link measurement there is when it is not. Two or more
unflagged entries are a choice the sketch cannot make, and it makes none — the
values stay unknown rather than being guessed.

| Knob | Default | Meaning |
| --- | --- | --- |
| `ZB_LQI_ENDPOINT` | 1 | 0 drops endpoint 12 and keeps the LQI console-only |
| `ZB_RSSI_ENDPOINT` | 1 | 0 drops endpoint 13 and keeps the RSSI console-only |
| `LINK_INTERVAL_S` | 300 | how often the neighbour table is read; one read yields both |
| `LQI_DELTA` | 10 | how far the LQI has to move from the last published value |
| `RSSI_DELTA` | 5 | the same for the RSSI, in dB |
| `LINK_RETRY_MS` | 1000 | shorter wait after a read that found no parent entry |
| `LINK_REPORT_HEARTBEAT_S` | 3600 | repeats the last published values; 0 disables it |
| `LOG_EVERY_READING` | 0 | 1 also prints the polls held back by a deadband |

The deadbands work exactly like the temperature one, and for the same reason: a
healthy link wanders by a few counts and a few dB, and none of that is worth an
over-the-air report. Each value has its own deadband and is published on its own,
which is why a line can say `published RSSI` alone. A join or rejoin bypasses both
once, since a rejoin may well be through a different parent.

Worth knowing:

- **Both only move when frames arrive.** LQI and RSSI are measured on reception,
  so they update when the parent sends something — a poll response, a read, a
  report acknowledgement. `setRxOnWhenIdle(true)` keeps that traffic flowing.
- **Both are read once right after a join or rejoin**, and then every
  `LINK_INTERVAL_S`. To walk the board around and compare candidate spots, lower
  that interval for the trip — there is no manual trigger, since the button does
  nothing but the factory reset.
- **`link: no parent to read, neighbour table holds 0 entries`** is normal for a
  second or two right after a join — the endpoints keep their last value rather
  than publishing a 0 that would look like a dead link. The next attempt then comes
  after `LINK_RETRY_MS` rather than a whole `LINK_INTERVAL_S`, and the line is
  printed once per join however many retries it takes. The count is in the line
  because an empty table and a table whose entries are all unusable are different
  faults, and nothing else tells them apart.
- **Both are repeated every `LINK_REPORT_HEARTBEAT_S`** even when neither has
  moved. That is not cosmetic: a good link sits still for days, so the deadbands
  would otherwise leave the join-time report as the only one ever sent — and that
  one goes out before the coordinator has bound the cluster, i.e. nowhere. See
  [an expose that stays N/A](#an-expose-that-stays-na).
- **The RSSI endpoint carries a unit, and that took an extra attribute.** The ZCL
  application types have no dBm in the list, so `zb_link_endpoint.cpp` writes the
  BACnet unit number for dBm (200) into the Analog Input cluster's optional
  `EngineeringUnits` attribute (0x0075), which is where a coordinator looks when
  the application type implies no unit of its own. Without it the value would
  arrive correct but bare.
- **Turning either endpoint off or on changes the endpoint list**, so it needs a
  factory reset and a re-pair like any other endpoint change. It leaves every other
  endpoint number alone, though: 12 and 13 are fixed, and nothing is derived from
  them.

## Console mirror

Endpoint 14 puts the last console line worth an event on the air, as text. It is
the same information the [serial console](#serial-console) shows, for the normal
case where no console is attached: the device is on a wall somewhere and the only
way to it is the network.

It only ever prints. There is nothing on it that can be written, by design — an
Analog Input is read-only by definition, and the text attribute is created
read-only as well.

What is mirrored is what the console calls an event in its own right:

| Line | When |
| --- | --- |
| `Zigbee connected` | every join and rejoin |
| `Zigbee link lost` | the radio contact went |
| `Button: held long enough - release to factory reset` | the hold reached `FACTORY_RESET_HOLD_MS` |
| `Button: released before the hold was over, no reset` | released too early |
| `Button on pin 9 … - ignoring it until it goes idle` | the pin reads pressed with nothing pressing it |
| `Button: idle now, back in use` | that pin went idle again, so the button is real after all |
| `Factory reset: clearing NVS and re-pairing` | sent while the network is still there, so the coordinator hears why the device leaves |
| `Reading interval (s) written from Zigbee: 300.00 -> 300` | a coordinator changed the interval or the delta and it moved |
| `slot 0 (28FF…): read failed, never read since boot`, `slot 1 (…) is configured but missing` | a sensor stopped answering — the wording says which kind, see [telling an empty slot from a sensor that has failed](#telling-an-empty-slot-from-a-sensor-that-has-failed) |
| `temp sensors (3 slots): 1 on the bus, 1 missing, 1 never seen` | the three counts changed, or a join happened |
| `slot 0 (28FF…): 85.00 C is the power-on default` | a sensor came back with the value its register holds after a reset |
| `1-Wire: no device responded to CONVERT T` | nothing on the bus at all |
| `FATAL: …`, `Flash partition '…' is missing`, `NVS open failed …`, `Zigbee failed to start …`, `scan: failed` | every error line the sketch prints |

The periodic work is deliberately **not** mirrored — the readings, the link polls,
the bus rescans, the join hints. Those are what the temperature and link endpoints
carry, they already have `LOG_EVERY_READING` to decide how loud they are on the
console, and a mirror carrying them would show nothing but the last temperature.
Neither are the diagnostics that sit *under* one of the lines above: the button
probe prints several lines about the wiring, and only its first line — the fault
itself — goes on the air.

`logEvent()` in `console.h` is the whole mechanism: it prints like
`Serial.printf()` does and hands the same line to the endpoint. A line printed
with `Serial.printf()` is console-only; a line printed with `logEvent()` is both.

**Two attributes, one endpoint.** ZCL has no cluster for text, so the line travels
in a character string attribute of its own (`MIRROR_TEXT_ATTR_ID`, 0xF000) — and
that attribute is added to the endpoint's *standard* Analog Input cluster rather
than to a private cluster. That is the part that makes it work at all: a report is
addressed through the binding table, a coordinator binds the clusters it
recognises, and a cluster it has never heard of is one it will not bind, so those
reports would be dropped here at the source. See [an expose that stays
N/A](#an-expose-that-stays-na).

The value of that cluster is not wasted either: it counts the mirrored lines, so it
is the sequence number of the text beside it. It is a plain number, which means it
is visible on any coordinator with no converter at all, and it changes with every
new line — which is what an automation can trigger on even where the text itself
cannot be read. A gap in the count is honest: it says lines were printed while the
device was off the air.

| Knob | Default | Meaning |
| --- | --- | --- |
| `ZB_MIRROR_ENDPOINT` | 1 | 0 drops endpoint 14 and keeps every line console-only |
| `MIRROR_TEXT_LEN` | 64 | how much of a line is carried; the rest is cut off |
| `MIRROR_TEXT_ATTR_ID` | 0xF000 | the attribute the text lives in |
| `MIRROR_REPORT_HEARTBEAT_S` | 3600 | repeats the current line; 0 disables it |

Worth knowing:

- **The line is repeated every `MIRROR_REPORT_HEARTBEAT_S`**, for the same reason
  the link values are: the report a new line produces goes to whoever is bound at
  that moment, and right after a join that is still nobody — which would make the
  line announcing the join the one line never seen.
- **A repeated line is not resent.** A read that fails every interval, or a slot
  that stays missing, changes the text to what the coordinator already has, and
  that costs no report — the same deadband idea the temperatures and the link use.
  The count only moves when the line does.
- **Nothing is buffered while the device is off the air.** The mirror holds one
  line, and a line printed with no network is overwritten by the next one; a join
  publishes the state as it is then, starting with `Zigbee connected`. So the boot
  lines — including a boot-time error — are console-only in practice, since the
  radio is not up yet when they are printed.
- **A short button press mirrors nothing**, because it prints nothing: the button
  does one thing, and a press below `FACTORY_RESET_HINT_MS` is not an event. What
  the button *does* produce is in the table above.
- **64 characters is a frame, not a preference.** An APS payload is around 80
  bytes, the report adds a ZCL header and the string its length byte, and a report
  is not fragmented — so a longer line would not arrive, it would be dropped.
- **The text always occupies its full length.** A ZCL string attribute is sized by
  the value it is created with, so the attribute is created at `MIRROR_TEXT_LEN`
  and every line is space-padded to it — the same thing the sensor ID does with
  LocationDescription. Whatever reads it wants a `trim()`.
- **Zigbee2MQTT shows the count out of the box and the text with a small
  converter** — see [showing the mirrored line](#showing-the-mirrored-line).
- **Turning the endpoint off or on changes the endpoint list**, so it costs a
  factory reset and a re-pair, exactly like the two link endpoints. It leaves every
  other endpoint number alone.

## Serial console

Three things here happen on a timer whether or not the result differs from the
last one: the sensors are read every interval, the link is polled every
`LINK_INTERVAL_S`, and the bus is rescanned every `ONEWIRE_RESCAN_INTERVAL_MS`
while a slot is empty. All three report **only when something changed**, which is
what keeps the lines that matter visible at 115200 baud:

| Happens every time | Printed |
| --- | --- |
| a reading past its deadband, published | yes |
| a reading inside its deadband | no |
| a value repeated by its heartbeat | yes, marked `(heartbeat)` |
| a read that a retry rescued | yes, `read retried`, console only |
| a read that failed every attempt, a slot going missing | yes, with what the slot had read before |
| a reading of exactly 85.00 °C, the register's power-on value | yes, once per spell of it |
| a scan finding the same sensors as last time | no |
| a scan finding a different number, or a new ROM code | yes |
| a scan whose slot counts changed, empty slots included | yes |
| joining, losing the link, a factory reset, a fault | yes |
| the banner and the configured slot count, once at boot | yes |

```c
#define LOG_EVERY_READING 1
```

That restores a line per reading and per link poll, the held-back ones included
(`within deadband`), which is the view to use when choosing `TEMP_DELTA_DEFAULT_C`,
`LQI_DELTA` or `RSSI_DELTA` — it shows what a given deadband would have suppressed.

While the device has no network the wait is still reported every
`JOIN_HINT_INTERVAL_S`, since there "nothing changed" is itself the news; see
[Joining a network](#joining-a-network).

The rows that are events in their own right — joining, losing the link, what the
button did, an error, a setting a coordinator changed — are exactly the ones that
also go out on endpoint 14, so they are readable without a console attached at all.
See [Console mirror](#console-mirror).

### Telling an empty slot from a sensor that has failed

A temperature expose with no value looks the same either way: a slot nothing was
ever plugged into, and a sensor that worked for a month and then stopped, both
read N/A. The console and the mirror are where the difference is stated.

Four states, and what each one prints:

| State | Line | On the air |
| --- | --- | --- |
| never assigned | `slot 2 is unassigned, no sensor has ever claimed it` | no, see below |
| assigned, not answering the bus scan | `slot 0 (28FF…) is configured but missing` | yes |
| answering the scan, every attempt bad, none ever good | `slot 0 (28FF…): read failed, never read since boot` | yes |
| answering the scan, every attempt bad, one was good | `slot 0 (28FF…): read failed, last good 21.4 C` + `that reading was 7 min ago` | the first line only |

The middle two are the useful pair. A slot only holds a ROM code because a scan
once found that sensor and wrote the code to NVS, so **`configured but missing`
is proof the sensor existed** — a slot that was never filled cannot produce that
line. And `read failed` says the sensor still answers the ROM search, so it is
powered and addressable, but the scratchpad came back with a bad CRC or an
impossible value: a marginal contact rather than a missing device.

**Which of those two you get is itself the diagnosis.** A sensor that is gone
goes missing at the scan; a sensor that is dying answers the scan and fails the
read, over and over as the rescan keeps finding it again.

`read failed` means every attempt failed. A bad CRC on one read is the ordinary
result of a noisy edge on a long cable, and the conversion it belongs to is still
sitting in the sensor's register, so asking again costs about 10 ms and usually
works — against a slot that would otherwise stay dark until the next rescan, up to
`ONEWIRE_RESCAN_INTERVAL_MS` away. `ONEWIRE_READ_RETRIES` in `config.h` is how
many extra attempts a slot gets (1 by default, 0 disables it), and a retry that
worked says so on the console:

```
slot 0 (28FF641E1234ABCD): read retried, attempt 2 succeeded
```

Console-only, deliberately: it is not a fault a coordinator can act on, and it
would cost a report every time the cable was noisy. It is still worth watching. A
bus that keeps needing the retry is telling you about the pull-up, the cable or
the supply, and the retry is rescuing readings rather than fixing anything.

Since the mirror holds exactly one line, the per-slot detail cannot all fit on
it, so one summary line goes out whenever the counts change:

```
temp sensors (3 slots): 1 on the bus, 1 missing, 1 never seen
```

That is the whole picture in one line: the slot count is `MAX_DS18B20_SENSORS` and
the three that follow add up to it, which is what makes `0 on the bus` readable —
without the total it could equally be a build with no slots at all. It is stable
while nothing changes — so the mirror's deadband suppresses the repeats — and it is
sent again after every join, because the only scan that ran before the radio came up
was the one in `setup()`, whose result reached nobody. With every slot reading, the
counts stop changing and the temperatures themselves are the better answer anyway.

The wording is as short as it is on purpose: at 61 characters it is inside the
mirror's `MIRROR_TEXT_LEN` of 64, so the copy on endpoint 14 is the whole line
rather than a cut one. It costs 57 plus one digit per number, which leaves room for
any slot count a single 1-Wire bus would carry. If you reword it, count the result —
64 is the most that fits an unfragmented report, so raising the limit is not free.

What is deliberately **not** mirrored: the unassigned-slot lines (three empty
slots would push each other off a one-line mirror, and an empty slot is not a
fault), and the age of the last good reading (it changes on every attempt, which
would make every failed read a fresh report). Both are on the console, where
there is room.

**85.00 °C is not a temperature, usually.** That exact value is what a DS18B20's
temperature register holds after a power-on reset, with a perfectly good CRC — so
it is what a sensor whose supply dips returns, and what any sensor returns if it
is read before its first conversion finishes. It is also a temperature a sensor
can legitimately be at, so the driver flags it rather than dropping it
(`DS18B20Reading::powerOnReset`) and the sketch publishes the value and says what
it means:

```
slot 0 (28FF641E1234ABCD): 85.00 C is the power-on default
  check the supply and the wiring
```

Once per spell of it, not once per interval, and the check is for that exact
value: 84.9375 °C and 85.0625 °C are one sixteenth of a degree away and are
treated as real readings. The usual cause is a pull-up or a supply that cannot
hold the sensor through a conversion — see [supply
voltage](#supply-voltage--check-this-before-powering-up).

### Why the first lines used to arrive mangled

The NanoH2 has no USB-to-UART bridge; the console is the H2's own USB Serial/JTAG
peripheral, which exists only while a host has the port open. A write issued before
that runs into the driver's transmit timeout, and what it discards is the remainder
of the *buffer* rather than the remainder of the line. So the earliest boot lines
were not lost, they were spliced:

```
Sensor slots: none confiefault)
```

That is `Sensor slots: none configured` cut off after 24 bytes, followed by the last
bytes of a `... (code default)` line printed further down — two lines, no such string
anywhere in the source.

```c
#define SERIAL_WAIT_MS 2000
```

`setup()` now waits that long for the host to open the port before printing anything.
The wait ends the moment the port is open, so a monitor that is already listening
costs nothing, and it is bounded so a headless device still boots. 0 restores the old
behaviour. If your console driver never reports the host as connected, the wait just
runs its full length and behaves like a plain delay, which is also fine.

## Zigbee2MQTT

No external converter is needed for the values: Z2M generates a definition for
unknown devices (`findByDevice(device, true)`), and its generator covers every
cluster used here. After pairing you get, under vendor `M5Stack` / model
`NanoH2-DS18B20`, with the default of three sensors:

| Expose | Access | Unit | From |
| --- | --- | --- | --- |
| `analog_out_duration_10` | read/write | s | endpoint 10, reading interval |
| `analog_out_temperature_11` | read/write | °C | endpoint 11, reporting delta |
| `analog_in_count_12` | read | — | endpoint 12, parent link LQI |
| `analog_input_13` | read | dBm | endpoint 13, parent link signal strength |
| `analog_in_count_14` | read | — | endpoint 14, how many console lines were mirrored |
| `temperature_20`, `temperature_21`, `temperature_22` | read | °C | endpoints 20-22 |

The one thing that is *not* in that list is the mirrored text itself: Z2M's
generator has no expose type for a string, so endpoint 14 arrives as its counter
alone — see [showing the mirrored line](#showing-the-mirrored-line).

The endpoint number is part of every name, which is why the settings and the link
endpoints have fixed numbers: a different sensor count then renames nothing but the
temperature exposes — see [Changing the sensor
count](#changing-the-sensor-count).

The order above is also the order Z2M generates them in. Its generator walks the
endpoints as the device reported them and groups the exposes by the cluster it
meets first, so registering the settings and the link endpoints ahead of the
temperature ones puts them first in the list rather than after the sensors. It is
the *device's* endpoint order that decides this, not the numbering — the numbering
just makes the two agree. Anything downstream is free to sort differently; Home
Assistant, for one, orders entities its own way.

The units and names come from the `applicationType` the sketch sets on each
analog cluster, which Z2M maps through the BACnet application type tables:
`ESP_ZB_ZCL_AI_TIME_RELATIVE` → *duration* / `s`, `ESP_ZB_ZCL_AI_TEMPERATURE_OTHER`
→ *temperature* / `°C`, `ESP_ZB_ZCL_AI_COUNT_UNITLESS_COUNT` → *count* / no unit.
The RSSI endpoint uses the *other* application type group, which maps to no name
and no unit, so it is `analog_input_13` and its `dBm` comes from `EngineeringUnits`
instead — Z2M falls back to that whenever the application type implies no unit.
The `description` attribute becomes the label (*Parent link LQI*, *Parent link
RSSI*), and min / max / resolution become the slider bounds and step. An Analog
*Input* is generated read-only (`STATE_GET`), which is what both of these should
be.

Worth knowing:

- The **ROM codes are not exposed automatically.** Z2M's generator ignores
  LocationDescription, so read it per endpoint from the dev console (Basic
  cluster, attribute `locationDesc`) to find out which sensor a slot holds, or
  add an external converter that exposes it.
- Z2M **rewrites the temperature reporting configuration** during `configure`
  (10 s / 1 h / 1.00 °C). That is expected and harmless here, because publishes
  are reported explicitly — see [above](#how-the-delta-gates-reporting).
- Z2M also tries to configure reporting on the Analog Output `presentValue`. If
  the Zigbee stack rejects it, `configure` is logged as failed and retried; the
  two settings still work, since Z2M reads them on demand and the sketch reports
  them explicitly whenever they change. A `configure` that fails part way through
  does have a cost, though — see below.

### An expose that stays N/A

`N/A` means Z2M has never had a value for that attribute. It says nothing about
the endpoint being wrong, and the first question is which side is quiet. The
console answers it:

| Console | Where the gap is |
| --- | --- |
| `link: parent 0x0000  LQI 168/255  RSSI -62 dBm  published …` | the device measured it and sent it — the gap is between the device and Z2M |
| `link: no parent to read, …` and never a `link: parent …` line | the device has nothing to send — the neighbour table is not yielding the parent |

For the second case the endpoints are the messenger, not the problem; there is
nothing to publish. For the first, the report went out and was dropped on the way,
and there is one usual reason. **A report is addressed through the binding
table**: `reportAnalogInput()` sends it with
`ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT`, so a cluster nothing is bound to
has its reports discarded at the source. Z2M creates those bindings in
`configure`, its generated definition asking for reporting on every
`presentValue`, which means:

- **A `configure` that failed leaves later endpoints unbound.** The steps run in
  expose order and one throwing ends the run, so a rejected Analog Output
  reporting configuration on endpoint 10 can cost the bindings for 12, 13, 14 and
  the temperatures behind it. Look for `failed to configure` in the Z2M log, then press
  **Reconfigure** on the device page.
- **A binding can be added by hand.** Device → *Bind*, source endpoint `12`, `13`
  or `14`, cluster `genAnalogInput`, destination *Coordinator*.
- **A read needs no binding at all**, which makes it the quickest proof that the
  device holds the value: dev console → endpoint `12` → `genAnalogInput` → read
  `presentValue`.

After a binding change the value arrives at the next heartbeat at the latest
(`LINK_REPORT_HEARTBEAT_S`, an hour), without waiting for the link to move — that
is what the heartbeat is there for. The temperature exposes work exactly the same
way and only look healthier because a temperature keeps moving.

A temperature expose is also the one case where `N/A` is often not a fault at
all: there is an endpoint per configured slot whether or not a sensor is in it,
so an empty slot has nothing to publish and stays `N/A` by design. Which of the
two it is, the console and the mirror say outright — see [telling an empty slot
from a sensor that has
failed](#telling-an-empty-slot-from-a-sensor-that-has-failed).

The [console mirror](#console-mirror) rides on this same mechanism, which is why
its text sits in a `genAnalogInput` cluster rather than in a cluster of its own: the
binding Z2M creates for the counter is what carries the text with it. A cluster Z2M
had never heard of would be bound by nothing, and every mirrored line would be
discarded here at the source.

### Showing the mirrored line

Endpoint 14 sends two things on every new line: `presentValue`, which Z2M exposes
as `mirror_line_count_14` — it names an expose after the cluster's `description`
attribute, which is why the sketch sets that to "Mirror line count" and not to
"Console mirror" — and the line itself in attribute `0xF000` (61440), a ZCL
character string. Z2M handles the first and has nowhere to put the second — its
generated definitions can express a number, not a string — so out of the box the
count increments and the text is only in the debug log.

Read it by hand, no converter involved: dev console → endpoint `14` →
`genAnalogInput` → read attribute `61440`. That works whenever the device is awake
and is the quickest way to answer "what happened".

To have it as an expose, an external converter has to do that mapping, and
[`nanoh2-ds18b20.mjs`](nanoh2-ds18b20.mjs) in this folder is that converter, ready
to use — see [Adding the external converter](#adding-the-external-converter). All
it adds to what Z2M generates by itself is one entry:

```js
m.text({
    name: 'console_mirror',
    cluster: 'genAnalogInput',
    attribute: {ID: 0xf000, type: 0x42},   // 61440, a ZCL character string
    description: 'Last console line worth an event, space padded',
    access: 'STATE_GET',
    endpointName: '14',                    // singular here, unlike m.numeric()
    entityCategory: 'diagnostic',
}),
```

`console_mirror` being free as a name is the point of calling the counter
[something else](#showing-the-mirrored-line). There is no `reporting` entry because
the device reports `0xF000` on its own, on every new line and on the hourly
heartbeat, so there is nothing for Z2M to configure.

Two things this costs. An external definition **replaces** the generated one for
this model, so whatever it does not list disappears from the device page — which is
why the file carries the settings, the link values and the temperatures too, and
why it has to be regenerated whenever the endpoint list changes. And `m.text()`
publishes the attribute verbatim, so the line arrives space padded to
`MIRROR_TEXT_LEN`. To have it trimmed instead, replace that entry with a
`fromZigbee` converter that does it:

```js
// Endpoint 14, genAnalogInput, attribute 0xF000: the mirrored console line.
const consoleMirror = {
    cluster: 'genAnalogInput',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg) => {
        if (msg.endpoint.ID !== 14 || msg.data['61440'] === undefined) return;
        // The string is space padded to MIRROR_TEXT_LEN, so trim it.
        return {console_mirror: String(msg.data['61440']).trim()};
    },
};
```

with a matching `e.text('console_mirror', ea.STATE)` in the definition's exposes.
That is the whole cost of the text, and it is why the counter beside it is a plain
number — it is the part that needs none of this.

### Adding the external converter

**Prerequisite: external JavaScript has to be enabled.** Z2M ignores converter
files silently while it is off, so this comes first. Either in
`configuration.yaml`:

```yaml
advanced:
  enable_external_js: true
```

or in the frontend under **Settings → Settings → Advanced → `enable_external.js`**,
which has to be checked. Both are the same switch; the frontend writes the same
line into `configuration.yaml`.

**Then add the file, preferably through the UI.** In the frontend, open
**Settings → External converters**, add a new converter, paste the contents of
[`nanoh2-ds18b20.mjs`](nanoh2-ds18b20.mjs) and save. Z2M stores it in its own
`external_converters/` directory and loads it at runtime, so no restart is needed
and a mistake in the file shows up as an error in the log straight away rather than
as a Z2M that will not come back up.

> **Tip — what to type as the filename.** Name it after the *model*, not the
> device: `nanoh2-ds18b20.mjs`. A definition is matched by the `zigbeeModel` string
> the device reports (`ZB_MODEL` in `config.h`), never by its IEEE address, so this
> one file serves every board running this sketch — and an address in the name
> would suggest a per-device scope that does not exist. It has to end in `.mjs`,
> and it is a plain name, not a path: Z2M decides the directory.

The manual route does the same thing by hand: copy the file into
`external_converters/` next to `configuration.yaml` (Home Assistant add-on:
`/config/external_converters/`) and **restart** Z2M. Use it when the frontend is
not reachable or when the file is deployed by configuration management; the UI is
otherwise the better way round, because it needs no restart.

Either way, check the result on the device page: **Exposes** should now show
*Console mirror* with a read button beside the *Mirror line count* number, and the
device's MQTT state should gain a `console_mirror_14` key. Pressing the button on
the board is the quickest end-to-end test — the console line it prints should
appear there.

A copy lives with the sketch so that the definition and the firmware it belongs to
stay in one place; Z2M keeps its own copy, so a change to one has to be carried
over to the other. **Regenerate it after any change to the endpoint list** — device
page → *Dev console* → *Generate external definition* — and re-add the `m.text()`
entry above to the result, which is the only part Z2M cannot produce by itself. A
different `MAX_DS18B20_SENSORS` is the usual reason, and needs a
[re-pair](#changing-the-sensor-count) anyway.

The temperature endpoints are the part that goes stale, and the file names them
twice — once in `m.deviceEndpoints()` and once in
`m.temperature({endpointNames: ['20', '21', '22']})`. **Both lists have to match
`MAX_DS18B20_SENSORS` in `config.h`**, which is 3 in the file as it ships: the
sketch creates one temperature endpoint per configured slot, numbered up from
`EP_TEMP_BASE` (20), and nothing on the device side adapts to a converter that
disagrees. Listing an endpoint the firmware does not have produces an expose that
stays `N/A` for good and a binding that fails during `configure`; listing one fewer
hides a sensor that is really reporting. Regenerating gets this right by
construction, since Z2M reads the endpoint list off the device — which is also why
it is the better move than editing the two lists by hand.

## Pushbutton

The button does **one** thing: the factory reset, which is how the device leaves a
network. It has no part in joining one — see [Joining a
network](#joining-a-network) — and nothing at all is bound to a short press.

Everything here holds for whichever button `PIN_BUTTON` points at, external or
on-board, with one exception: on the on-board `G9` — the default — the hold must
happen on a running device, because holding that pin through power-up flashes the
board instead. See [Which button](#which-button).

- **Hold 5 s** (`FACTORY_RESET_HOLD_MS`) **and release** — clear everything: the
  commissioning flag, the interval, the delta and the slot ↔ ROM mapping from our
  NVS namespace, plus the Zigbee stack's own network credentials via
  `Zigbee.factoryReset()`. The device reboots into a factory-fresh state and
  flashes magenta again.
- **Anything shorter** — nothing happens. `Button: released before the hold was
  over, no reset` if the hold had already been armed.

The LED follows the hold: red once it passes `FACTORY_RESET_HINT_MS`, then white
at `FACTORY_RESET_HOLD_MS` to say the reset happens as soon as you let go.

**The reset fires on the release, not during the hold.** That is deliberate, and
it is what makes a faulty pin harmless: a pin stuck at the active level reads
pressed and never lets go, so it never produces a release. Firing during the hold
instead meant such a pin wiped the credentials five seconds into *every* boot and
rebooted into the same state — a device that never stayed up long enough to join,
and that looked for all the world as if joining needed the button pressed.

Two guards back that up:

- **A pin that already reads pressed in `setup()`** cannot be a real press —
  nobody was holding the button before power-on — so the button is ignored until
  it reads idle once, and the reason is printed.
- **A press lasting longer than `BUTTON_STUCK_MS`** (30 s) is not a hand either,
  so it is inhibited the same way, with the same message.

Both then probe the pin instead of guessing, because a logic level on its own
cannot say *why* it is at the wrong end — this is the full report, from an external
button on `G2`; on the on-board `G9` the millivolt line and the arithmetic under it
are absent:

```
Button on pin 2 already reads pressed - ignoring it until it goes idle
  the pin reads LOW, and with BUTTON_ACTIVE_HIGH 0 that counts as pressed
  probe: pulled towards pressed LOW, pulled towards idle LOW
  probe: 8 mV on the pin with no internal pull, rail is 3300 mV
  the 45 kOhm internal pull cannot move the pin, so tens of microamps are flowing
  in: a conductive path is holding it, not noise and not leakage. With the button
  open the pin should sit at the idle rail - measure it there. A 4-pin tactile
  switch shorts the two legs on the same side, which leaves the contact closed
  for good, and a rail wire in the signal position does the same thing
  an external pull resistor also does it - ~10 kOhm beats the 45 kOhm internal one.
  If the open button measures a few hundred mV off the rail rather than on it, that
  is what it is, and BUTTON_ACTIVE_HIGH 0 is the wrong way round for this wiring
  also check that PIN_BUTTON and PIN_ONEWIRE match the Grove wiring (see README)
```

The pin is read with the pull applied both ways, and — on `G1`…`G5`, which are
`ADC1_CH0`…`CH4` on this chip, so not on the on-board `G9` — its open-circuit
voltage is measured too. That
separates the causes that look identical from a `digitalRead()`:

| Probe result | Cause |
| --- | --- |
| the idle pull moves the level | nothing is holding the pin; the reading was pick-up on a long run, and a 10 kΩ resistor at the button end fixes it far better than the internal 45 kΩ one |
| the idle pull cannot move it, voltage at the *active* rail | a conductive path: a contact that never opens, or a supply rail in the signal position |
| the idle pull cannot move it, voltage between the rails | a resistive path fighting the internal pull, and one of the two is winning by too little — see below |

The arithmetic behind that reading: the internal pulls are 45 kΩ typical, `VIH` is
0.75 × VDD and `VIL` is 0.25 × VDD, so holding the pin at the wrong end takes
about **55 µA** through it, while the pin's own input leakage is at most 50 nA
(ESP32-H2 datasheet, Table 5-3). Three orders of magnitude apart — which is why a
level that will not budge means a wire, not interference.

The same arithmetic run backwards turns a voltage between the rails into a
resistance, and that is worth doing rather than guessing: an external resistor
`R` against the internal 45 kΩ leaves the idle pin at
`3.3 V × 45 kΩ / (45 kΩ + R)` — a 10 kΩ pull-up fighting the internal pull-down
of an `BUTTON_ACTIVE_HIGH 1` build lands at ≈ 2.7 V, i.e. just *above* the 2.48 V
`VIH`, so the open contact reads as a press with only ~200 mV of margin. The cure
is not a bigger resistor but the right polarity: with `BUTTON_ACTIVE_HIGH 0` the
internal pull-up and that external one pull the same way, the idle level sits at
the full 3.3 V, and a closed contact shorts the pin to 0 V. Whenever the two
pulls oppose each other, the polarity is set the wrong way round.

An inhibited button starts working the moment the level goes idle (`Button: idle
now, back in use`) — no reboot needed. If it goes idle only while you *press* it,
the polarity is inverted: flip `BUTTON_ACTIVE_HIGH`.

## Notes and limits

- **Readings never reach NVS.** Only configuration goes into flash: the
  commissioning flag, the interval, the delta and the slot ↔ ROM mapping. Each
  of those is written once, when it changes — the mapping when a new sensor takes
  a free slot, the flag on the first join, a setting only when the value actually
  moved. Temperatures live in RAM (`lastPublished[]`) and go out over the air,
  because a value written every interval would spend the flash's write
  endurance for nothing. Keep it that way when extending the sketch.
- **No `OneWire` / `DallasTemperature` dependency, because `OneWire` does not
  build for this chip.** Its direct-GPIO layer (`util/OneWire_direct_gpio.h`, 2.3.8)
  special-cases the ESP32-C3 only; every other ESP32 takes the "plain ESP32" branch,
  which reads `GPIO.in` as a scalar and references `GPIO.in1` / `GPIO.out1_w1tc` /
  `GPIO.enable1_w1tc`. The H2 has 27 GPIOs and none of those registers, and the ones
  it does have are unions needing `.val`, so it is a compile error rather than a
  timing problem — and both halves of the `pin < 32` test are compiled, so the pin
  number does not save it. The C3 path is exactly right for the H2, so the
  alternative is a patched fork: extend the five
  `#if CONFIG_IDF_TARGET_ESP32C3` sites with `|| CONFIG_IDF_TARGET_ESP32H2` (and C6,
  C2 while there). `ds18b20_bus.cpp` implements reset, read/write slots, the Maxim
  ROM search and CRC-8 directly instead, masking interrupts only for the parts of
  each time slot that have an upper bound — which also keeps that logic where
  `test/onewire` can check it on the host. `DallasTemperature` itself would be no
  trouble; what it adds beyond this file is per-sensor resolution and alarms, and
  this sketch uses neither.
- **Swapping a sensor keeps the endpoint.** A sensor discovered at runtime takes
  the first free slot, starts reporting temperature straight away, and its ROM
  code is written into that endpoint's LocationDescription immediately — no
  reboot and no re-interview. A coordinator that cached the attribute will still
  show the old value until it reads it again.
- **An empty bus is silent.** When no slot holds a sensor — nothing connected
  yet, or no slots configured — the interval passes without starting a
  conversion, so there is no failed-conversion line every interval. The periodic
  rescan (`1-Wire scan: 0 DS18B20 found`) is the only output until a sensor turns
  up, and the device stays joined and answers reads the whole time.
- **Failed reads keep the last value.** A CRC error is retried straight away
  (`ONEWIRE_READ_RETRIES`); a slot that fails every attempt is logged and dropped
  until the next rescan (`ONEWIRE_RESCAN_INTERVAL_MS`), and the endpoint keeps its
  previous temperature rather than publishing a bogus one.
- **A device flashed with the first version of this sketch** stored the interval
  as a `uint32`, where it is now a float blob. The typed read fails cleanly and
  the interval falls back to the code default once, then persists normally. A
  factory reset avoids the question entirely.
- **Not a sleepy end device.** `setRxOnWhenIdle(true)` is required so the
  coordinator's interval writes can reach the device. Fine on USB power; a
  battery build would need a different approach to configuration.
- **The console mirror is a mirror, not a log.** It holds one line — the last one
  — so a burst of events is seen as its last member plus a counter that jumped.
  Reconstructing what happened in between needs the serial console, and boot-time
  lines never make it out at all, because the radio is not up when they are
  printed. See [Console mirror](#console-mirror).
- **Two open upstream issues touch this sketch.**
  [arduino-esp32#12917](https://github.com/espressif/arduino-esp32/issues/12917):
  seventeen of the core's report helpers leave `manuf_code` — the key the stack
  looks an attribute up by — uninitialised, while the same library sets it
  correctly on the configure-reporting path in ten other places. That is why
  `ZbMirror::reportText()` builds its report command itself: zeroed first, then
  `manuf_code` set to `ESP_ZB_ZCL_ATTR_NON_MANUFACTURER_SPECIFIC` by name rather
  than by number, because the next SDK generation keeps that name and changes its
  value from `0xFFFF` to `0x0000`. Every other report here — the temperatures, the
  link values, the mirror's own counter — still goes through a core helper and so
  is still on the uninitialised path; it works because the stack ignores the field
  for a report that is not manufacturer specific.
  [esp-zigbee-sdk#909](https://github.com/espressif/esp-zigbee-sdk/issues/909)
  asks the other half: why a report the stack rejects aborts the device at
  `esp_zigbee_zcl_command.c:263` instead of returning an error. That abort was
  seen here, right after a join, and its cause was never identified — if it comes
  back, erase the flash and pair fresh before looking anywhere else.
