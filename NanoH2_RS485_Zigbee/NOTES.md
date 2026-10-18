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

## Which Data a Node Can Send

The bus itself doesn't tell. RS485 only defines the electrical side: two
wires, voltage levels, half-duplex. What travels over it is set by a
protocol on top, by far most often **Modbus RTU**. Modbus has no way for a
device to describe itself: what a node delivers is in its **register map**,
from the manufacturer's documentation.

### How Modbus RTU Works

- **Only answers when asked:** one master (the NanoH2) and nodes (slaves)
  with an address from 1 to 247. A node only replies to a request addressed to
  it.
- **Function codes for reading:** `03` holding registers (settings and
  values), `04` input registers (measured values, read-only), `01` and `02`
  single bits (coils, discrete inputs).
- **Registers are 16 bits.** A 32-bit value, such as a float or a meter
  reading, spans two registers, and the word order (which half comes first)
  differs between manufacturers.

### What the Register Map Tells You

For each value:

| Item | Example (fictitious energy meter) |
| --- | --- |
| Register address | `0x0000` |
| Function code | `04` (input register) |
| Data type | float32, 2 registers, high word first |
| Unit / scale factor | V, or "value × 0.1" for an int16 |
| Meaning | voltage L1 |

Plus the **serial settings**: baud rate, parity and stop bits (for example
9600 8N1 or 9600 8E1), and the default node address. Without matching
settings there is no answer at all.

### When There's No Documentation

1. **Ask the manufacturer or search:** "\<model\> Modbus register map" or
   "Modbus protocol PDF". Many devices have a separate protocol document next
   to the manual.
2. **Community configurations:** ready-made register maps for popular devices,
   for example in evcc, Home Assistant `modbus:` configurations or Tasmota
   templates. Energy meters like the Eastron SDM series are well covered.
3. **Device identification:** some devices answer function `43/14` (Read
   Device Identification) with manufacturer and model. That says which device
   it is, not which values it has.
4. **Self-describing standards:** a few device classes do describe
   themselves, for example **SunSpec** on many PV inverters: the supported
   models can be read from a fixed start register.
5. **Sniffing:** if the device already talks to another controller, such as a
   heat-pump display, connect an RS485 adapter in parallel and only listen.
   The requests and replies show which registers the controller reads.
6. **Scanning:** reading register ranges one after another works, but gives
   numbers without meaning that then have to be guessed. **Only ever read,
   never write:** an unknown write can change a device's settings.

Not every RS485 device speaks Modbus. Others use **M-Bus** (meters, but a
different electrical layer), **DLMS** (smart meters) or a proprietary
protocol. The device's documentation says which.
