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

The pure-logic parts — the 1-Wire driver and the settings — have host tests in
[`../test/`](../test/); run them with `cd test && make`.

## Wiring

The Grove HY2.0-4P port carries `GND` (black), `5V` (red), `G2` (yellow) and
`G1` (white).

| Signal | Pin | Notes |
| --- | --- | --- |
| DS18B20 data | `G2` (Grove yellow) | all sensors in parallel, one 4.7 kΩ pull-up to their supply rail |
| Pushbutton | `G1` (Grove white) | to `GND`; internal pull-up is enabled in software |
| RGB LED | `G11` | on-board WS2812 |
| RGB power | `G10` | on-board, must be driven high or the LED stays dark |

Swap `PIN_ONEWIRE` and `PIN_BUTTON` in `config.h` if your wiring is the other
way round.

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
*Disabled* and *Default 4MB with spiffs*; both have to be changed by hand or the
sketch will not link.

To enter download mode: hold the on-board `G9` button, *then* plug in USB-C.

## LED

| State | LED |
| --- | --- |
| Never joined a network | magenta, flashing on a 3 s cycle |
| Joined and on the air | solid green |
| Joined before, radio lost | yellow, flashing on a 3 s cycle |
| Factory-reset hold in progress | solid red |
| Factory reset accepted | solid white, then reboot |

Cycle length and duty are `LED_FLASH_CYCLE_MS` / `LED_FLASH_DUTY_PCT`; all
colours are `LedColor` constants at the top of `config.h`.

## Zigbee endpoints

With the default of three sensors:

| Endpoint | Cluster | Purpose |
| --- | --- | --- |
| 10, 11, 12 | Temperature Measurement | one per sensor slot |
| 13 | Analog Output | reading interval, seconds |
| 14 | Analog Output | reporting delta, °C |

An Analog Output cluster carries a single value, so each writable setting needs
its own endpoint.

The numbers are all derived from `EP_TEMP_BASE` and `MAX_DS18B20_SENSORS`: slots
take `EP_TEMP_BASE … EP_TEMP_BASE + MAX_DS18B20_SENSORS - 1`, then the two
settings follow directly above them, so they can never collide with a
temperature endpoint.

Each temperature endpoint exposes all three required identifiers:

- **static ID** — the endpoint number (`EP_TEMP_BASE` + slot, so 10 + slot by
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

Everything else follows it — the endpoint objects, the endpoint numbers, the
`romN` keys in NVS, the slot arrays and every loop over them. Nothing else in the
sources needs editing, and two `static_assert`s in the sketch catch the two ways
of getting it wrong: a count below 1, and a count so large that the settings
endpoints would run past the Zigbee maximum of 240.

What changes on the air:

- **The settings endpoints move.** They sit directly above the last temperature
  slot, so with four sensors they become 14 and 15 rather than 13 and 14. In
  Zigbee2MQTT the expose names follow (`analog_out_duration_14`,
  `analog_out_temperature_15`), which breaks automations and dashboards that
  reference the old names.
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
| Endpoint | 13 | 14 |

(Endpoints 13 and 14 with the default of three sensors; see [Changing the sensor
count](#changing-the-sensor-count).)

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

## Zigbee2MQTT

No external converter is needed: Z2M generates a definition for unknown devices
(`findByDevice(device, true)`), and its generator covers every cluster used here.
After pairing you get, under vendor `M5Stack` / model `NanoH2-DS18B20`, with the
default of three sensors:

| Expose | Access | Unit | From |
| --- | --- | --- | --- |
| `temperature_10`, `temperature_11`, `temperature_12` | read | °C | endpoints 10-12 |
| `analog_out_duration_13` | read/write | s | endpoint 13, reading interval |
| `analog_out_temperature_14` | read/write | °C | endpoint 14, reporting delta |

The endpoint number is part of every name, so a different sensor count renames
these exposes — see [Changing the sensor
count](#changing-the-sensor-count).

The units and names come from the Analog Output `applicationType` the sketch
sets, which Z2M maps through the BACnet application type tables:
`ESP_ZB_ZCL_AI_TIME_RELATIVE` → *duration* / `s`, `ESP_ZB_ZCL_AI_TEMPERATURE_OTHER`
→ *temperature* / `°C`. The `description` attribute becomes the label, and
min / max / resolution become the slider bounds and step.

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

- **Short press** — take a reading immediately instead of waiting out the interval.
- **Hold 5 s** (`FACTORY_RESET_HOLD_MS`) — clear everything: the commissioning
  flag, the interval, the delta and the slot ↔ ROM mapping from our NVS
  namespace, plus the Zigbee stack's own network credentials via
  `Zigbee.factoryReset()`. The device reboots into a factory-fresh state and
  flashes magenta again.

The LED turns red once the hold passes `FACTORY_RESET_HINT_MS`, so the reset is
never a surprise.

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
