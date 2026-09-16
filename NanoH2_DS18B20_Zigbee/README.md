# NanoH2 DS18B20 Zigbee sensor

Three DS18B20 temperature sensors on one 1-Wire bus, published over Zigbee from
an M5Stack NanoH2 (ESP32-H2, SKU C149), with a pushbutton for factory reset and
the on-board RGB LED as a link-state indicator.

Three is only the default; see [Changing the sensor
count](#changing-the-sensor-count).

## Files

| File | Contents |
| --- | --- |
| `NanoH2_DS18B20_Zigbee.ino` | Application: link state machine, LED, sampling, button |
| `config.h` | Every tunable: pins, colours, flash cycle, sensor count, interval, delta, hold time |
| `ds18b20_bus.h/.cpp` | Self-contained 1-Wire master and DS18B20 driver |
| `zb_setting.h/.cpp` | A setting with a code default, an NVS override and a Zigbee override |
| `zb_temp_endpoint.h/.cpp` | Temperature endpoint that also publishes its sensor's ROM code |
| `zb_link.h/.cpp` | LQI and RSSI of the link to the parent, from the neighbour table |
| `zb_link_endpoint.h/.cpp` | Analog input endpoint that can state its unit, for the RSSI in dBm |

The pure-logic parts — the 1-Wire driver, the settings and the link lookup — have host tests in
[`../test/`](../test/); run them with `cd test && make`.

## Wiring

The Grove HY2.0-4P port carries `GND` (black), `5V` (red), `G2` (yellow) and
`G1` (white).

| Signal | Pin | Notes |
| --- | --- | --- |
| DS18B20 data | `G1` (Grove white) | all sensors in parallel, one 4.7 kΩ pull-up to their supply rail |
| Pushbutton | `G2` (Grove yellow) | one side to `G2`, other side to `GND`; internal pull-up enabled in software |
| RGB LED | `G11` | on-board WS2812 |
| RGB power | `G10` | on-board, must be driven high or the LED stays dark |

Swap `PIN_ONEWIRE` and `PIN_BUTTON` in `config.h` if your wiring is the other
way round.

> **The cable colours are not reliable.** `G1` = white and `G2` = yellow is what
> the NanoH2 prints next to its Grove port, but the cable you plug in may well be
> the other way round: white on `G2` and yellow on `G1`. Only `5V` = red and
> `GND` = black are dependable. Ring the cable out with a multimeter, or go by the
> serial log. A swapped pair puts the sensors' 4.7 kΩ pull-up on the button pin
> and the button on the bus, which reads as:
>
> ```
> 1-Wire scan: 0 DS18B20 found
> ```
>
> …while the button does nothing at all, because that pull-up holds its pin at the
> idle level whatever the contact does. Swap `PIN_ONEWIRE` and `PIN_BUTTON` in
> `config.h`.

The button is wired the same way as in the [pushbutton
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

To enter download mode: hold the on-board `G9` button, *then* plug in USB-C.

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
| `JOIN_SCAN_INTERVAL_S` | 120 | how often to scan; 0 keeps the hint and never scans |
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
| 20, 21, 22 | Temperature Measurement | one per sensor slot |

An Analog Output cluster carries a single value, and so does an Analog Input one,
so every setting and every measurement needs an endpoint of its own.

What describes the device comes first, what it measures last: the two settings,
the two link measurements, then the temperature slots from `EP_TEMP_BASE` up. The
numbers themselves carry no meaning — any assignment within 1 … 240 is legal — so
the order is a readability choice, and the endpoints are registered in the same
order, because that is the order in which the stack reports them and the order a
coordinator lists them in.

The four low ones are fixed constants in `config.h`, deliberately *not* derived
from the sensor count, so changing that count leaves them — and the names a
coordinator derives from their numbers — untouched. The gap between 13 and
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
ways of getting it wrong: a negative count, and a count so large that the
temperature endpoints would run past the Zigbee maximum of 240.

**Zero is a valid count.** With `MAX_DS18B20_SENSORS 0` there are no temperature
endpoints, the settings and the two link endpoints stay at 10 … 13, and
`PIN_ONEWIRE` is never driven at all — no bus scan, no conversions, and
`1-Wire bus unused, no slots to map a sensor onto` in place of the scan line. What is left is
the Zigbee side, the two settings, the
[link quality](#link-quality-and-signal-strength), the LED and the button,
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

- **The settings and link endpoints stay where they are**, at 10 … 13, because
  their numbers are fixed rather than derived from the count. Only the temperature
  endpoints change: a slot is added above the last one or removed from the top, so
  in Zigbee2MQTT the count of `temperature_2x` exposes changes and the rest of the
  names do not.
- **The device has to be re-paired.** The endpoint list is fixed at
  `Zigbee.begin()`. After flashing, hold the button for 5 s and release to factory
  reset, then join again. In Zigbee2MQTT, delete the device first and let it re-interview;
  otherwise Z2M keeps serving the cached definition with the old endpoint set.

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
| a failed read, a slot going missing | yes |
| a scan finding the same sensors as last time | no |
| a scan finding a different number, or a new ROM code | yes |
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

## Zigbee2MQTT

No external converter is needed: Z2M generates a definition for unknown devices
(`findByDevice(device, true)`), and its generator covers every cluster used here.
After pairing you get, under vendor `M5Stack` / model `NanoH2-DS18B20`, with the
default of three sensors:

| Expose | Access | Unit | From |
| --- | --- | --- | --- |
| `analog_out_duration_10` | read/write | s | endpoint 10, reading interval |
| `analog_out_temperature_11` | read/write | °C | endpoint 11, reporting delta |
| `analog_in_count_12` | read | — | endpoint 12, parent link LQI |
| `analog_input_13` | read | dBm | endpoint 13, parent link signal strength |
| `temperature_20`, `temperature_21`, `temperature_22` | read | °C | endpoints 20-22 |

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
  reporting configuration on endpoint 10 can cost the bindings for 12, 13 and the
  temperatures behind it. Look for `failed to configure` in the Z2M log, then press
  **Reconfigure** on the device page.
- **A binding can be added by hand.** Device → *Bind*, source endpoint `12` or
  `13`, cluster `genAnalogInput`, destination *Coordinator*.
- **A read needs no binding at all**, which makes it the quickest proof that the
  device holds the value: dev console → endpoint `12` → `genAnalogInput` → read
  `presentValue`.

After a binding change the value arrives at the next heartbeat at the latest
(`LINK_REPORT_HEARTBEAT_S`, an hour), without waiting for the link to move — that
is what the heartbeat is there for. The temperature exposes work exactly the same
way and only look healthier because a temperature keeps moving.

## Pushbutton

The button does **one** thing: the factory reset, which is how the device leaves a
network. It has no part in joining one — see [Joining a
network](#joining-a-network) — and nothing at all is bound to a short press.

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
cannot say *why* it is at the wrong end:

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
`ADC1_CH0`…`CH4` on this chip — its open-circuit voltage is measured too. That
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
- **Failed reads keep the last value.** A CRC error or a missing sensor is
  logged and the slot is retried on the next rescan (`ONEWIRE_RESCAN_INTERVAL_MS`);
  the endpoint keeps its previous temperature rather than publishing a bogus one.
- **A device flashed with the first version of this sketch** stored the interval
  as a `uint32`, where it is now a float blob. The typed read fails cleanly and
  the interval falls back to the code default once, then persists normally. A
  factory reset avoids the question entirely.
- **Not a sleepy end device.** `setRxOnWhenIdle(true)` is required so the
  coordinator's interval writes can reach the device. Fine on USB power; a
  battery build would need a different approach to configuration.
