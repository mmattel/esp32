# NanoH2 RS485 — Notes

Planning notes for an RS485 project on the M5Stack NanoH2. Nothing is built or
tested yet; the sketch is an empty placeholder.

## When to Use RS485

RS485 is the go-to choice for **wired, robust connections over long distances
in noisy environments**, wherever data volumes are small. It has been the
backbone of industrial and building automation for decades, which is why so
many devices still offer it.

### Typical Applications

- **Energy meters and smart meters:** electricity meters (Eastron SDM, many
  DIN-rail meters), heat meters, sometimes water meters, mostly over Modbus RTU.
- **Photovoltaics and storage:** inverters, battery management systems and
  wallboxes, often over Modbus to their energy manager or as a local interface.
- **Heating and ventilation:** heat pumps, ventilation units and boiler
  controllers, often over Modbus, sometimes with proprietary protocols.
- **Industry:** PLCs, frequency converters, motor drives, sensors and
  actuators in machines (Modbus RTU, Profibus DP).
- **Building automation:** room controllers, valve and fan controls
  (BACnet MS/TP, Modbus).
- **Stage, lighting and displays:** **DMX512** for stage lighting is RS485
  too, as are many LED displays and access-control readers.

### When It's the Right Choice

- **Long distances:** up to about **1200 m** at low baud rates, far beyond TTL
  serial (a few metres) or I²C (centimetres).
- **Electrical noise:** the differential signal (A minus B) on twisted pair
  cancels out interference from motors, pumps, inverters and mains cables.
- **Several devices on one cable:** up to 32 standard loads per segment, or
  more with modern low-load transceivers.
- **Reliability without radio:** cellars, metal cabinets, concrete or buried
  cables, or where wireless isn't wanted.
- **The device simply has it:** often the deciding reason. If a heat pump or
  meter only offers Modbus RTU, RS485 is the way in.

### When Not to Use It

- **High data rates:** typically 9600–115200 baud, at most a few Mbit/s on
  short lines. For video, large files or streaming, use Ethernet.
- **Several devices that talk on their own:** RS485 has no built-in
  arbitration. Modbus solves this with one master polling the others, which
  means waiting time and no instant events. Use CAN when nodes must send on
  their own (vehicles, battery systems), or Ethernet or radio.
- **Star-shaped cabling:** RS485 wants a **line** (daisy chain) with short
  stubs. A star with long branches causes reflections, and termination becomes
  guesswork.
- **Nothing to connect to:** if the devices speak Zigbee, Wi-Fi or Ethernet
  anyway, an RS485 cable only adds work.
- **Potential differences without isolation:** between buildings or separate
  electrical systems, ground potentials can differ by several volts. That
  calls for **galvanically isolated** transceivers, or fibre.
- **Battery-powered sensors:** a transceiver and bus termination draw power
  continuously. For a battery sensor, Zigbee is the better fit.

### For This Project

The NanoH2 as a **gateway**: it reads a meter, heat pump or inverter over RS485
(Modbus RTU) and sends the values to Zigbee2MQTT over Zigbee. Useful for a
device that only offers RS485, with the NanoH2 right next to it and the RS485
line short, inside the cabinet or plant room.

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
