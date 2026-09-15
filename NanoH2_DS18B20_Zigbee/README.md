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
| Pushbutton | `G2` (Grove yellow) | one side to 3.3 V, other side to `G2`; internal pull-down enabled in software |
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
> Button on pin 2 already reads pressed - ignoring it until it goes idle
> 1-Wire scan: 0 DS18B20 found
> ```
>
> Either line on its own is enough; swap `PIN_ONEWIRE` and `PIN_BUTTON` in
> `config.h`.

The button is wired the same way as in the [pushbutton
sketch](../NanoH2_Button_LED/): it feeds 3.3 V into the pin when closed, and the
weak internal pull-down (tens of kΩ) holds the pin low while the contact is open.
Take the 3.3 V from a 3V3 pad, not from the Grove 5 V rail — see below. For a
long run to the button, add an external 10 kΩ pull-down from `G2` to `GND` so
induced noise cannot register as a press. A button that closes to `GND` instead
needs one line in `config.h`:

```c
#define BUTTON_ACTIVE_HIGH 0
```

That switches the pin to `INPUT_PULLUP` and inverts the level test.

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
| Factory reset accepted | solid white, then reboot |
| Cannot run at all | red, flashing on a 3 s cycle; reason on the serial console |

Cycle length and duty are `LED_FLASH_CYCLE_MS` / `LED_FLASH_DUTY_PCT`; all
colours are `LedColor` constants at the top of `config.h`.

## Joining a network

A factory-fresh device flashes magenta and looks for a network to join. The stack
retries that — *network steering* — once a second, indefinitely, and it logs the
failures only at **Core Debug Level → Info**; at the default `None` a device that
can find nothing to join looks exactly like one that is not trying at all.

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

Everything else follows it — the endpoint objects, the temperature endpoint
numbers, the `romN` keys in NVS, the slot arrays and every loop over them. Nothing
else in the sources needs editing, and `static_assert`s in the sketch catch the
ways of getting it wrong: a negative count, and a count so large that the
temperature endpoints would run past the Zigbee maximum of 240.

**Zero is a valid count.** With `MAX_DS18B20_SENSORS 0` there are no temperature
endpoints, the settings and the two link endpoints stay at 10 … 13, and
`PIN_ONEWIRE` is never driven at all — no bus scan, no conversions. What is left is
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
  `Zigbee.begin()`. After flashing, hold the button for 5 s to factory reset, then
  join again. In Zigbee2MQTT, delete the device first and let it re-interview;
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
| Code default | `TEMP_INTERVAL_DEFAULT_S` — 60 s | `TEMP_DELTA_DEFAULT_C` — 0.5 °C |
| Range | 10 … 3600 s | 0 … 20 °C |
| Step | 1 s | 0.1 °C |
| Endpoint | 10 | 11 |

Both resolve the same way, each source overriding the one above it:

1. The code default in `config.h`.
2. The value stored in NVS by a previous run.
3. A value written to the analog output endpoint from the coordinator.

A write is rounded to the step, clamped to the range, persisted to NVS and
mirrored back to the analog output attribute — so a clamped or rounded write
shows up on the coordinator rather than silently diverging. The interval minimum
stays above the 750 ms conversion time of a 12-bit reading. Both live in NVS, so
they survive a reboot; a factory reset restores the code defaults.

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

`TEMP_REPORT_HEARTBEAT_S` (the ZCL `max_interval`, 1 h by default) repeats the
last published value even while readings sit inside the deadband, so a quiet
device still produces traffic; set it to 0 to disable periodic reports entirely.

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
link: parent 0x0000  LQI 171/255  RSSI -61 dBm  within deadband
link: parent 0x0000  LQI 174/255  RSSI -74 dBm  published RSSI
```

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

| Knob | Default | Meaning |
| --- | --- | --- |
| `ZB_LQI_ENDPOINT` | 1 | 0 drops endpoint 12 and keeps the LQI console-only |
| `ZB_RSSI_ENDPOINT` | 1 | 0 drops endpoint 13 and keeps the RSSI console-only |
| `LINK_INTERVAL_S` | 300 | how often the neighbour table is read; one read yields both |
| `LQI_DELTA` | 10 | how far the LQI has to move from the last published value |
| `RSSI_DELTA` | 5 | the same for the RSSI, in dB |
| `LINK_RETRY_MS` | 1000 | shorter wait after a read that found no parent entry |
| `LINK_REPORT_HEARTBEAT_S` | 3600 | repeats the last published values, `max_interval` |

The deadbands work exactly like the temperature one, and for the same reason: a
healthy link wanders by a few counts and a few dB, and none of that is worth an
over-the-air report. Each value has its own deadband and is published on its own,
which is why a line can say `published RSSI` alone. A join or rejoin bypasses both
once, since a rejoin may well be through a different parent.

Worth knowing:

- **Both only move when frames arrive.** LQI and RSSI are measured on reception,
  so they update when the parent sends something — a poll response, a read, a
  report acknowledgement. `setRxOnWhenIdle(true)` keeps that traffic flowing.
- **A short button press reads them immediately**, which is the point: you can walk
  the board around and press the button at each candidate spot instead of waiting
  out `LINK_INTERVAL_S`.
- **`link: parent not in the neighbour table yet`** is normal for a second or two
  right after a join — the endpoints keep their last value rather than publishing a
  0 that would look like a dead link. The next attempt then comes after
  `LINK_RETRY_MS` rather than a whole `LINK_INTERVAL_S`, and the line is printed
  once per join however many retries it takes.
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
  them explicitly whenever they change.

## Pushbutton

- **Short press** — take a reading immediately instead of waiting out the
  interval, and read the [link quality and signal
  strength](#link-quality-and-signal-strength) along with it.
- **Hold 5 s** (`FACTORY_RESET_HOLD_MS`) — clear everything: the commissioning
  flag, the interval, the delta and the slot ↔ ROM mapping from our NVS
  namespace, plus the Zigbee stack's own network credentials via
  `Zigbee.factoryReset()`. The device reboots into a factory-fresh state and
  flashes magenta again.

The LED turns red once the hold passes `FACTORY_RESET_HINT_MS`, so the reset is
never a surprise.

A pin that already reads *pressed* during `setup()` cannot be a real press —
nobody was holding the button before power-on — so the button is ignored until it
goes idle once, and the reason is printed. Without that, a stuck or miswired pin
would run the 5 s hold a few seconds into the first `loop()`, factory reset, and
come back up to do it again: a device that wipes its credentials on every boot
and never stays joined long enough to be paired. The message names the pin, and
the button starts working the moment the level goes idle (`Button: idle now, back
in use`) — no reboot needed.

## Notes and limits

- **Readings never reach NVS.** Only configuration goes into flash: the
  commissioning flag, the interval, the delta and the slot ↔ ROM mapping. Each
  of those is written once, when it changes — the mapping when a new sensor takes
  a free slot, the flag on the first join, a setting only when the value actually
  moved. Temperatures live in RAM (`lastPublished[]`) and go out over the air,
  because a value written every interval would spend the flash's write
  endurance for nothing. Keep it that way when extending the sketch.
- **No `OneWire` dependency.** `OneWire`'s direct-GPIO layer only special-cases
  ESP32-C3 and C6; on the H2 it takes the "plain ESP32" branch and references
  `GPIO.in1` / `GPIO.out1_w1ts`, registers this SoC does not have, so it will
  not compile. `ds18b20_bus.cpp` implements reset, read/write slots, the Maxim
  ROM search and CRC-8 directly, masking interrupts only for the parts of each
  time slot that have an upper bound.
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
