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
rather than a copy that can drift. Where a test needs a private member, it is
reached with a local `#define private public` around the include — a deliberate
shortcut that keeps the production headers free of test hooks.

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

### `zb_link` — parent link quality

The `Zigbee.h` stub declares the neighbour-table types and prototypes verbatim as
the Arduino core's Zigbee headers do, so a change in how `zb_link.cpp` spells them
fails to compile here rather than only on the board. The table itself is an array
the test fills, and the stack lock is counted.

Covers:

- The parent is picked by `relationship`, not by position: an entry behind a child
  and a sibling is still found.
- Nothing to report — stack not started, empty table, two unflagged entries, lock
  unavailable — returns `valid=false`, and never leaves the lock held.
- The fallback: a table with exactly one entry that is not flagged as the parent is
  read as the parent anyway, with `assumed` set. Two of them are not, since that is
  a choice the code cannot make. A flagged parent never sets `assumed`.
- `entries` is the size of the whole table, not how far the search got: 0 when it is
  empty, 2 for two non-parent entries, and 3 when the parent sits in the middle.
- `lqi` 255 and `rssi` -128 pass through untruncated, with the sign kept.

### `zb_mirror` — console mirror

The `Zigbee.h` stub keeps `setClusterAttribute()` protected, as the core has it, and
stores a written string the way the stack does: it copies as many bytes as the
string's leading length byte says. So these tests read back the bytes a coordinator
would receive — padding and all — instead of the sketch's own buffer, which is the
only way the padding rule can be checked at all. `millis()` comes from a variable
the test sets, so the hour-long heartbeat is reached without waiting for it. The
stub `Arduino.h` includes `<climits>` on purpose: the real one reaches the POSIX
limit macros through FreeRTOS, and a sketch constant that collides with one of them
(`LINE_MAX` did) now fails here rather than only on the board.

Covers:

- The text attribute as `addText()` creates it: in the manufacturer range, a
  character string, read-only and reportable, and created at full length in spaces —
  which is what the stack sizes it from.
- A published line: written to the analog input cluster's text attribute, padded to
  `MIRROR_TEXT_LEN` so a shorter line cannot leave the tail of a longer one behind,
  a `trim()` away from what was printed, with the line count beside it and both
  reported through the binding table from this endpoint.
- The deadband: a line repeated three times costs one report and does not move the
  count; the line before last counts as new again.
- A leading indent is dropped, and a line longer than `MIRROR_TEXT_LEN` is cut, not
  overrun.
- Off the air: nothing is written or reported, the line stays pending and goes out
  when the radio is back, and the count says a line went by meanwhile.
- `forgetPublished()` sends the same line again, which is what gets the join line
  out to a coordinator that was not bound yet.
- The heartbeat: not a moment early, on time, restarting from the last report rather
  than from the last new line, never while disconnected, and never before anything
  has been printed.
- The failure paths: without the text attribute, and with a write the stack refuses,
  the line count still goes out.
- `logEvent()` formats once for both the console and the mirror, and a line past its
  own buffer is cut at both ends of that path rather than overrunning either.

## What these tests do not cover

They are host tests with stubs, so anything that only exists on the device is out
of scope: real 1-Wire timing and interrupt masking, the Zigbee stack (including
whether the neighbour table actually holds a parent entry when we look, whether it
really stores a character string by its length byte, and whether a coordinator binds
the cluster the mirrored line rides on), the cluster
attribute plumbing in `zb_link_endpoint.cpp`, NVS itself, the RGB LED, and the
sketch's own state machines (link state, joining, sampling phases, button handling)
which live in the `.ino` and are not compiled here. A green run
is not a substitute for flashing the board.
