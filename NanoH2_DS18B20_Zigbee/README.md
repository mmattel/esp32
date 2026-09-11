# NanoH2 DS18B20 Zigbee sensor

Three DS18B20 temperature sensors on one 1-Wire bus, published over Zigbee from
an M5Stack NanoH2 (ESP32-H2, SKU C149), with a pushbutton for factory reset and
the on-board RGB LED as a link-state indicator.

## Files

| File | Contents |
| --- | --- |
| `NanoH2_DS18B20_Zigbee.ino` | Application: link state machine, LED, sampling, button |
| `config.h` | Every tunable: pins, colours, flash cycle, interval, delta, hold time |
| `ds18b20_bus.h/.cpp` | Self-contained 1-Wire master and DS18B20 driver |
| `zb_setting.h/.cpp` | A setting with a code default, an NVS override and a Zigbee override |

The pure-logic parts — the 1-Wire driver and the settings — have host tests in
[`../test/`](../test/); run them with `cd test && make`.

## Wiring

The Grove HY2.0-4P port carries `GND` (black), `5V` (red), `G2` (yellow) and
`G1` (white).

| Signal | Pin | Notes |
| --- | --- | --- |
| DS18B20 data | `G2` (Grove yellow) | all three sensors in parallel, 4.7 kΩ pull-up to their supply rail |
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

| Setting | Value |
| --- | --- |
| Board | **ESP32H2 Dev Module** |
| Zigbee mode | **Zigbee ED (end device)** |
| Partition Scheme | **Zigbee 4MB with spiffs** |
| USB CDC On Boot | **Enabled** |

There is no `m5stack_nanoh2` board definition in arduino-esp32 yet, so the
generic H2 board is the one to pick. Requires arduino-esp32 3.x for the bundled
`Zigbee` library.

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

| Endpoint | Cluster | Purpose |
| --- | --- | --- |
| 10, 11, 12 | Temperature Measurement | one per sensor slot |
| 13 | Analog Output | reading interval, seconds |
| 14 | Analog Output | reporting delta, °C |

An Analog Output cluster carries a single value, so each writable setting needs
its own endpoint.

Each temperature endpoint exposes all three required identifiers:

- **static ID** — the endpoint number (10 + slot). The slot ↔ sensor mapping is
  stored in NVS, so slot 0 keeps meaning the same physical sensor across
  reboots even if bus enumeration order changes.
- **sensor internal ID** — the DS18B20's 64-bit ROM code, in the Basic
  cluster's model identifier: `DS18B20-S0-28FF641E1234ABCD`.
- **temperature** — the measured value, reported on the configured interval and
  whenever it moves by `TEMP_REPORT_DELTA_C`.

All three endpoints exist whether or not a sensor is plugged in, because the
endpoint list is fixed at `Zigbee.begin()` and cannot grow later without
re-pairing.

## Reading interval and reporting delta

Two separate knobs: the **interval** is how often the bus is read, the **delta**
is how much a reading has to move before it is published.

| | Interval | Delta |
| --- | --- | --- |
| Code default | `TEMP_INTERVAL_DEFAULT_S` — 60 s | `TEMP_DELTA_DEFAULT_C` — 0.5 °C |
| Range | 10 … 3600 s | 0 … 20 °C |
| Step | 1 s | 0.1 °C |
| Endpoint | 13 | 14 |

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
**more than** the delta. The gate is the attribute write itself: within the
deadband the temperature attribute is left untouched, so there is nothing for
the stack to report. That keeps the behaviour intact even if the coordinator
rewrites the ZCL reporting configuration during its interview — no attribute
change means no delta report, whatever it configured.

`setReporting(1, 0, delta)` mirrors the same intent into the ZCL configuration:
report on a change of at least delta, never on a timer.

Two consequences worth knowing:

- Reading the attribute directly returns the last *published* value, not the
  instantaneous one. By construction it is within the delta of reality.
- With no change there are no reports at all, by design. Coordinators that infer
  availability from traffic (Zigbee2MQTT's availability feature, for one) may
  mark the device offline during a long quiet spell. If that is a problem, ask
  the coordinator to configure a periodic report, or add a maximum-silence
  timer to `readAndPublish()`.

Right after a join or a rejoin the deadband is bypassed once per sensor
(`lastPublished` is reset to `NAN`), so the coordinator always starts with real
values instead of waiting for the first threshold crossing.

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

- **No `OneWire` dependency.** `OneWire`'s direct-GPIO layer only special-cases
  ESP32-C3 and C6; on the H2 it takes the "plain ESP32" branch and references
  `GPIO.in1` / `GPIO.out1_w1ts`, registers this SoC does not have, so it will
  not compile. `ds18b20_bus.cpp` implements reset, read/write slots, the Maxim
  ROM search and CRC-8 directly, masking interrupts only for the parts of each
  time slot that have an upper bound.
- **Swapping a sensor needs a reboot to re-advertise.** A sensor discovered at
  runtime starts reporting temperature straight away, but the ROM code in the
  model string is fixed when the endpoint is built. Reboot to refresh it, and
  re-interview the device on the coordinator.
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
