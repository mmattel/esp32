# Host tests

Tests for the parts of [`NanoH2_DS18B20_Zigbee`](../NanoH2_DS18B20_Zigbee/) that
are pure logic, run on the build machine with `g++`. No board, no toolchain, no
Zigbee network needed.

```
cd test && make
```

`make` builds and runs every suite and exits non-zero if any assertion fails.
`make clean` removes the binaries.

Each suite compiles the **real sketch sources** against stub headers in its own
directory, so the tests always exercise what is in `NanoH2_DS18B20_Zigbee/`
rather than a copy that can drift. Private members are reached with a local
`#define private public` around the include — a deliberate shortcut that keeps
the production headers free of test hooks.

## Suites

### `onewire` — 1-Wire master and DS18B20 driver

The stub `Arduino.h` redirects `digitalWrite`, `digitalRead` and
`delayMicroseconds` into a simulated bus. The simulator reconstructs 1-Wire slot
semantics from the call sequence and the recorded low-pulse length — a long low
is a reset, ~60 µs is a write-0, a short low is either a write-1 or a read slot,
resolved by whether a read follows — and answers as a collective of slaves.
That makes the real ROM search and CRC-8 run end to end.

Covers:

- CRC-8 against the reference implementation from PaulStoffregen/OneWire, over
  256 vectors.
- `discover()` on one, three and eight devices, including eight that share a
  six-byte ROM prefix, which is what stresses the search's discrepancy tracking.
- An empty bus: nothing found, no hang.
- Mixed families: a DS18S20 (`0x10`) sharing the bus is skipped, the DS18B20
  (`0x28`) is not.

### `zb_setting` — writable settings

The `Preferences` stub mirrors the real float behaviour: values are stored as a
blob, and a miss leaves the caller's `NAN` default in place. That is exactly what
the NAN sentinel in `ZbSetting::load()` depends on, so the stub has to keep this
property for the precedence test to mean anything.

Covers, for both the interval and the delta:

- Step rounding and clamping at both ends of the range; `NAN` falls back to the
  code default.
- Precedence: code default when NVS is empty, NVS over the default, and a stored
  out-of-range value still clamped on load.
- `applyPending()`: no-op without a pending write, change detection on a repeated
  write, persistence to NVS, and mirror-back of the effective value after a
  clamped write.

## What these tests do not cover

They are host tests with stubs, so anything that only exists on the device is out
of scope: real 1-Wire timing and interrupt masking, the Zigbee stack, NVS itself,
the RGB LED, and the sketch's own state machines (link state, sampling phases,
button handling) which live in the `.ino` and are not compiled here. A green run
is not a substitute for flashing the board.
