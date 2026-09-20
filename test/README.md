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
That makes the real ROM search, CRC-8 and scratchpad read run end to end.

Every bit the master writes goes through one `sim_masterBit()`, whichever way it
was signalled, so the write-0 and write-1 paths cannot drift apart as commands
are added. The commands modelled are SEARCH ROM (`0xF0`), MATCH ROM (`0x55`),
SKIP ROM (`0xCC`), WRITE SCRATCHPAD (`0x4E`, counted and dropped) and READ
SCRATCHPAD (`0xBE`). Each device carries a nine-byte scratchpad set by
`sim_setScratch(dev, raw, goodCrc)`, which fills in the rest of the bytes as a
12-bit DS18B20 holds them and computes the check byte — or corrupts it on
request.

Covers:

- CRC-8 against the reference implementation from PaulStoffregen/OneWire, over
  256 vectors.
- `discover()` on one, three and eight devices, including eight that share a
  six-byte ROM prefix, which is what stresses the search's discrepancy tracking.
- An empty bus: nothing found, no hang.
- Mixed families: a DS18S20 (`0x10`) sharing the bus is skipped, the DS18B20
  (`0x28`) is not.
- `read()` addressing: two devices with different scratchpads on the bus, each
  read by its own ROM code. A driver that ignored the ROM would answer from
  whichever device came first, and would fail this.
- `read()` conversion: a positive value (`0x0158` → 21.5 °C) and a negative one
  (`0xFF5E` → −10.125 °C), both exact in a float so the comparison is safe.
- The `powerOnReset` flag: `0x0550` is +85.00 °C, the temperature register's
  power-on value, so it comes back **valid and flagged** — the CRC is good and
  85 °C is a temperature a sensor can really be at, which is why the driver
  reports it rather than dropping it. One sixteenth either side (`0x054F`,
  `0x0551`) is not flagged, which is what makes it an exact-value test rather
  than a threshold.
- `read()` rejections, one per cause: a corrupted check byte, a value past the
  sensor's 125 °C with a good check byte, a ROM that is not on the bus (nobody
  answers MATCH ROM, the read slots return all ones, and `0xFF` × 9 fails the
  CRC), and an empty bus that fails at the reset.
- `romToChars()` against `romToString()`: the same 16 digits from both, terminated
  inside `ROM_CHARS`, family code first — the low byte of the packed value — and
  every hex digit exercised in every position, since a gap in the digit table
  would misprint one sensor's address in sixteen.

### `slots` — what the sensor slots are doing

The suite for [`slots.cpp`](../NanoH2_DS18B20_Zigbee/slots.cpp): which of the four
states a slot is in, and which line says so. Two things are watched separately and
they are not the same. What went **on the air** is what went through `logEvent()`,
which the test captures; what only the **console** saw is everything else printed,
which the stub `Serial` captures as well. Several of the rules here are exactly
that split — the mirror carries one line, so a detail that changes on every
attempt has to stay off it — and a test that read the console alone would pass
while the mirror filled with noise.

`MAX_DS18B20_SENSORS` and `LOG_EVERY_READING` are **pinned** in the test rather
than taken from `config.h`: both are knobs a build is expected to change (no slots
at all is a documented configuration), and these tests are about the rules, not
about the numbers they happened to be run with. `config.h` is included first so
the real values are what gets replaced. `millis()` comes from a variable the test
sets, so the age of a reading is checked without waiting for it.

Covers:

- The summary counts: every state in one line, the three numbers always adding up
  to the configured slot count, and a slot that answers the bus counted as on it.
- The summary's **length budget**. It is the one line built from four numbers, and
  the one that has overflowed while being worded: the test pins it at 57
  characters plus a digit per number and checks that against `MIRROR_TEXT_LEN`, so
  a rewording that spends the slack fails here instead of arriving truncated at the
  coordinator.
- The summary deadband: an unchanged bus costs one line rather than one per scan, a
  sensor turning up or going missing is reported, and `slotsForgetSummary()` sends
  the same counts again — which is what gets the summary out after a join.
- The 85 °C power-on default: said once per spell of it however many readings come
  in, said again after a real reading in between or after the slot lost its sensor,
  counted per slot, and with the advice line kept off the air.
- The two read failures told apart: never read since boot versus a last good value,
  the last good value surviving the sensor going missing (the slot keeps its ROM
  code, so it is still that sensor's reading and no other's), forgotten by
  `slotsBegin()`, and per slot rather than per device. The age of the reading is
  printed but never mirrored, since it changes on every attempt and would cost a
  report each time.
- `slotsForgetSlot()` against `slotsSensorGone()`, which is the difference between a
  sensor that is expected back and a slot that has been released for a replacement:
  a released slot reports "never read since boot" for whatever reads there next,
  since carrying the old value over to a new sensor's ROM code would be the one
  wrong answer available. Neighbouring slots keep their own history.
- `ageText()` at every boundary it has — under a second, the switch to minutes, the
  switch to hours, and 49 days, the far end of `millis()` — that the longest of them
  fits `AGE_TEXT_CHARS`, and that a buffer too small is cut rather than overrun.

### `zb_setting` — writable settings

The `Preferences` stub mirrors the real float behaviour: values are stored as a
blob, and a miss leaves the caller's `NAN` default in place. That is exactly what
the NAN sentinel in `ZbSetting::load()` depends on, so the stub has to keep this
property for the precedence test to mean anything.

Covers, across the interval, the delta and the temperature correction:

- Step rounding and clamping at both ends of the range; `NAN` falls back to the
  code default.
- Precedence: code default when NVS is empty, NVS over the default, and a stored
  out-of-range value still clamped on load.
- `applyPending()`: no-op without a pending write, change detection on a repeated
  write, persistence to NVS, and mirror-back of the effective value after every
  write that changed something — clamped, rounded or taken as sent — while a write
  that changed nothing stays silent.
- The negative half of all of that, which only the correction's range reaches:
  rounding away from zero on the low side, a value below the minimum clamped to
  it, 0 left exactly as it is since that is what switches the correction off, and
  a negative value written, persisted, read back after a reboot and mirrored back
  after a clamp — a lost minus sign would move every reading by twice the
  correction, in the wrong direction.
- That the endpoint says which firmware it is: `addEndpoint()` offers `FW_VERSION`
  to the Basic cluster exactly once. The suite defines
  `SwBuildAnalog::addSoftwareBuildId()` itself as a recorder rather than compiling
  [`zb_version.cpp`](../NanoH2_DS18B20_Zigbee/zb_version.cpp) in, so the check
  costs an assertion instead of a second set of stubs. A settings endpoint that
  stopped carrying the version would be silent on the board — a coordinator reads
  Basic from one endpoint of its choosing, and it does not have to be this one.

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
- An entry with no measurement in it — LQI 0 with RSSI +127, what the stack holds
  between creating the parent's entry at the join and hearing a frame from it — is
  `unmeasured` rather than valid, keeps the parent's address so the wait can name it,
  and passes no numbers on. The sole-entry fallback makes the same distinction
  instead of publishing the nonsense with `assumed` set, a measured parent behind
  such an entry is still found, and LQI 0 with RSSI 0 stays a measurement: only the
  impossible sign is evidence.

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
- Every field of the report command, including the ones the sketch has no opinion
  about. `manuf_code` is the key the stack looks the attribute up by, and the text
  attribute is added under no manufacturer code at all, so that is what the report has
  to ask for. The core's own helpers never set it
  ([arduino-esp32#12917](https://github.com/espressif/arduino-esp32/issues/12917)),
  which is why the sketch builds this command itself and why the check is here. The
  test fills the stack with a pattern first (`poisonStack()`) so a field nobody set
  reads as garbage here rather than as a lucky zero — with a pattern chosen not to
  collide with the value that is actually correct.
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
  the line count still goes out — a text that was not written is not reported, and
  the console is told once per spell of failing and once again on recovery, since
  the stack's own `log_e()` is compiled out at the Core Debug Level the board is
  built with. The `Serial` stub keeps what it printed so that line can be counted
  and read.
- `logEvent()` formats once for both the console and the mirror, and a line past its
  own buffer is cut at both ends of that path rather than overrunning either.

### `zb_version` — firmware version endpoint

The suite for [`zb_version.cpp`](../NanoH2_DS18B20_Zigbee/zb_version.cpp): the two
attributes it creates and what goes on the air when it publishes. The attribute
creation calls are recorded rather than performed, because what matters is exactly
what the sketch asks the stack for — which cluster, which attribute, which type,
which access, and a ZCL character string whose leading length byte agrees with the
characters after it. That length byte is the part no compiler checks and the board
cannot be asked about. The `Zigbee.h` stub keeps `_cluster_list` protected, as the
core has it: that is the whole reason `SwBuildAnalog` is a subclass, and if the core
ever makes it public this suite says so.

Covers:

- That the version on the console and the version on the air are one: `FW_VERSION`
  parses into three numbers, `FW_VERSION_NUMBER` encodes the same three, and the
  string fits `VERSION_TEXT_MAX`. Both come from the defines in `config.h` and so
  cannot disagree — unless somebody replaces one with a literal, which is what this
  catches.
- `SWBuildID` as `addSoftwareBuildId()` creates it: on the **Basic** cluster, at
  `0x4000`, a character string, read-only and *not* reportable — Basic is read
  during the interview, so reporting access would be asking for something unused —
  with a length byte that matches and nothing padded onto it.
- The three ways the stack can answer: no Basic cluster at all is refused with
  nothing created; an SDK that already made `0x4000` refuses the add, and then the
  value is updated instead, so the attribute holds this build's version either way;
  both refused is refused.
- A version too long is refused outright rather than cut — a cut version reads as a
  different version. The longest string that fits is taken whole, one character more
  is not, and nothing is created when it is not.
- The text attribute as `addText()` creates it: on the **analog input** cluster, at
  `VERSION_TEXT_ATTR_ID`, read-only **and** reportable — unlike `SWBuildID`, since
  publishing after a join is what keeps the expose from sitting empty until somebody
  presses read — and created at its own length, unpadded. The mirror pads because a
  longer line comes later; a version cannot change without a reflash.
- `publish()`: the number written, one report each, and **the text before the
  number**, so a coordinator acting on the number already has the string. Every
  field of the report command is checked, `manuf_code` included, for the same reason
  as in `zb_mirror` and with the same `poisonStack()` first.
- Off the air: nothing written, nothing reported, and nothing held back for later
  either — the next join publishes it again, and the answer cannot have changed in
  between.
- Without the text attribute the number still goes out, which is the half that works
  with no converter, since it is the cluster's own value.

## What these tests do not cover

They are host tests with stubs, so anything that only exists on the device is out
of scope: real 1-Wire timing and interrupt masking, the Zigbee stack (including
whether the neighbour table actually holds a parent entry when we look, whether it
really stores a character string by its length byte, whether a coordinator binds
the cluster the mirrored line rides on, and which endpoint it reads Basic from), the
cluster
attribute plumbing in `zb_link_endpoint.cpp`, NVS itself, the RGB LED, and the
sketch's own state machines (link state, joining, sampling phases, button handling,
and the bus scan that maps ROM codes onto slots and decides when a read is retried)
which live in the `.ino` and are not compiled here. A green run
is not a substitute for flashing the board.
