# NanoH2 RS485 — Notes

Planning notes for an RS485 project on the M5Stack NanoH2. Nothing is built or
tested yet; the sketch is an empty placeholder.

## Converter: Seeed Studio Grove – RS485

- **Board:** [Seeed Studio Grove – RS485](https://wiki.seeedstudio.com/Grove-RS485/)
  ([at RS Components](https://uk.rs-online.com/web/p/communication-wireless-development-tools/1887080)),
  with the MAX13487E transceiver
  ([Analog Devices](https://www.analog.com/en/products/MAX13487E.html)).
- **Why this one:** the MAX13487E switches between sending and receiving by
  itself (AutoDirection). RS485 is half-duplex, and an ordinary MAX485 or
  MAX3485 needs a third pin (DE/RE) for that. The NanoH2's Grove port has only
  two signal pins, G1 and G2, which is exactly TX and RX.
- **It has a Grove plug,** so it connects to the NanoH2 directly. The UART goes
  to G1/G2 with `Serial1.begin(baud, SERIAL_8N1, PIN_RX, PIN_TX)`; no DE pin is
  needed.

## Logic Level Converter and Step-Down

The NanoH2's Grove port supplies **5 V**, and the MAX13487E is a 5 V part.
Powered at 5 V, the module's RX output (towards the NanoH2) can be at 5 V, and
the ESP32-H2's pins are not 5 V tolerant. Seeed lists "3.3V/5V", but doesn't
say whether the board shifts the logic levels itself. So, as in the DS18B20
and WaterFlow builds:

- **A logic level converter (LLC)** 3.3 V ↔ 5 V on both signal lines,
  TX and RX. Use the bidirectional MOSFET (TX) channels of the converter,
  not the RX voltage-divider channels; see the WaterFlow README, "Note on the
  logic level converter".
- **A step-down converter 5 V → 3.3 V** to supply the LLC's LV side, because
  the Grove port has no 3.3 V.

Before ordering, check the module's schematic: if it does shift the levels
on board, the LLC and the step-down aren't needed.

## Cabling

- **Cable:** a 2-wire shielded twisted pair (STP) with a characteristic
  impedance of **120 Ω**. A and B go on the twisted pair.
- **Termination resistors:** one **120 Ω** resistor across A and B, **only at
  the two absolute ends** of the main bus line. None anywhere in between.
- **Shield grounding:** connect the cable shield to Earth ground at **a single
  point**, usually at the host or master controller end. Grounding both ends
  creates a ground loop, which acts like an antenna for electrical noise and
  can destroy the transceivers.
