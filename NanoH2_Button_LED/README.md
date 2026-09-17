# NanoH2 pushbutton indicator

A pushbutton on `G2` of an M5Stack NanoH2 (ESP32-H2, SKU C149) shown on the
on-board RGB LED: **green** while the contact is closed, **yellow** while it is
open. No Zigbee, no radio, no stored state.

## Contents

- [Files](#files)
- [Wiring](#wiring)
  - [Inverting the wiring](#inverting-the-wiring)
  - [Using the on-board button instead](#using-the-on-board-button-instead)
- [Arduino IDE settings](#arduino-ide-settings)
- [LED](#led)
- [Serial output](#serial-output)
- [Notes and limits](#notes-and-limits)

## Files

| File | Contents |
| --- | --- |
| `NanoH2_Button_LED.ino` | Application: LED helper, debounce, `loop()` |
| `config.h` | Every tunable: pins, colours, active level, debounce time |

## Wiring

The Grove HY2.0-4P port carries `GND` (black), `5V` (red), `G1` (white) and `G2`
(yellow).

| Signal | Pin | Notes |
| --- | --- | --- |
| Pushbutton | `G2` (Grove yellow) | other side to `GND`; internal pull-up enabled in software |
| RGB LED | `G11` | on-board WS2812 |
| RGB power | `G10` | on-board, must be driven high or the LED stays dark |

Change `PIN_BUTTON` in `config.h` to move the button to `G1` (Grove white), or to
`9` for the on-board button — see [Using the on-board button
instead](#using-the-on-board-button-instead).

> **The cable colours are not reliable.** `G1` = white and `G2` = yellow is what
> the NanoH2 prints next to its Grove port, but the cable you plug in may well be
> the other way round: white on `G2` and yellow on `G1`. Only `5V` = red and
> `GND` = black are dependable. Ring the cable out with a multimeter, or just try
> it — the sketch logs `G2 closed -> green` on every accepted press, so a button
> that does nothing usually means the two signal wires are swapped.

The button needs nothing but the contact and `GND`: `INPUT_PULLUP` holds `G2` at
3.3 V while the contact is open, and closing it takes the pin to 0 V. Measuring
`G2` against `GND` is the quickest way to confirm the wiring before flashing —
about 3.3 V with the button open, 0 V with it pressed.

The internal pull-up is weak (45 kΩ typical), which is fine for a button on short
wiring. For a long run to the button, add an external 10 kΩ pull-up from `G2` to
3.3 V so induced noise cannot register as a press. A ready-made button breakout
usually has that resistor on board already, wired to its `VCC` pin.

### Inverting the wiring

A button that feeds 3.3 V into the pin instead — needs one line in `config.h`:

```c
#define BUTTON_ACTIVE_HIGH 1
```

That switches the pin to `INPUT_PULLDOWN` and inverts the level test, so a closed
contact still means green. Nothing else changes. Take that 3.3 V from the board's
own 3V3 rail — the Grove connector only exposes **5 V**, and 5 V on a GPIO of a
3.3 V MCU damages the pin. Confirm against the [NanoH2
schematic](https://docs.m5stack.com/en/core/NanoH2) which pad exposes 3.3 V; do
not substitute the Grove 5 V rail, and do not rely on a resistive divider from it.

### Using the on-board button instead

`PIN_BUTTON 9` reads the board's own button, next to the USB-C socket, and needs no
wiring at all:

```c
#define PIN_BUTTON 9
```

No other change — the debounce and the level test are written in terms of
`PIN_BUTTON` and `BUTTON_ACTIVE_HIGH`, and `BUTTON_ACTIVE_HIGH 0` is right for the
on-board button too, since it also pulls the pin to `GND` when closed.

One thing to expect: `G9` is the H2's boot strapping pin, so it is also the flashing
button. Holding it while the board powers up enters ROM download mode and the sketch
never runs — the LED stays dark and the console stays silent. Press it after the
sketch is up and it behaves like any other button.

Worth knowing what this sketch is for, though: it exists to prove out an external
button and its wiring before that button is trusted for the factory reset in the
[Zigbee sketch](../NanoH2_DS18B20_Zigbee/), and on `G9` the Grove port goes unused.
As a quick check that the on-board button works it is still useful.

## Arduino IDE settings

| Setting | M5Stack core | Espressif core |
| --- | --- | --- |
| Board Manager URL | [`package_m5stack_index.json`](https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json) | [`package_esp32_index.json`](https://espressif.github.io/arduino-esp32/package_esp32_index.json) |
| Board | **M5Stack → M5NanoH2** | **ESP32H2 Dev Module** |
| Zigbee mode | **Disabled** | **Disabled** |
| Partition Scheme | **Default 4MB with spiffs** | **Default 4MB with spiffs** |
| USB CDC On Boot | **Enabled** | **Enabled** |

Both are the defaults, so unlike the [DS18B20
sketch](../NanoH2_DS18B20_Zigbee/) nothing has to be changed by hand here.
Prefer `M5NanoH2` — its variant already carries the right clock (96 MHz) — but
the generic board works too, since the sketch names every pin itself in
`config.h`.

To enter download mode: hold the on-board `G9` button, *then* plug in USB-C. With
`PIN_BUTTON 9` that is the same button the sketch reads — see [Using the on-board
button instead](#using-the-on-board-button-instead).

## LED

| State | LED |
| --- | --- |
| `G2` low (button closed) | solid green |
| `G2` high (button open) | solid yellow |

Both colours are `LedColor` constants in `config.h`. The LED is written on every
pass through `loop()`, but `ledWrite()` skips the WS2812 update when the colour
is unchanged, so the bus is only driven on an actual transition.

## Serial output

Every accepted transition is logged at 115200 baud, plus one line at startup so
the initial state is visible:

```
G2 open -> yellow
G2 closed -> green
```

## Notes and limits

- **Debounced, not interrupt-driven.** `loop()` samples the pin every 10 ms and a
  level has to hold for `BUTTON_DEBOUNCE_MS` (20 ms) before it counts. Contact
  bounce therefore never reaches the LED, at the cost of a press shorter than
  ~20 ms being ignored. Raise the debounce for a noisier switch, lower it for a
  clean one.
- **The open contact is never floating.** The internal pull-up (or pull-down, with
  `BUTTON_ACTIVE_HIGH 1`) is what makes "no signal" a defined level. Disabling it
  would leave the LED flickering between green and yellow with nothing connected.
- **Nothing is stored.** There is no NVS use at all, so the sketch always starts in
  whatever state the button is actually in.
