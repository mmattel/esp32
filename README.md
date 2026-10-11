# esp32

ESP32 sketches and firmware projects.

| Project | Target | Description |
| --- | --- | --- |
| [NanoH2_DS18B20_Zigbee](NanoH2_DS18B20_Zigbee/) | M5Stack NanoH2 (ESP32-H2) | DS18B20 sensors published over Zigbee, three by default, with factory-reset button and RGB link-state LED |
| [NanoH2_WaterFlow_Zigbee](NanoH2_WaterFlow_Zigbee/) | M5Stack NanoH2 (ESP32-H2) | Hall-effect water flow sensor published over Zigbee — flow rate, cumulative total and NVS writeback, with factory-reset button and RGB link-state LED |
| [NanoH2_Button_LED](NanoH2_Button_LED/) | M5Stack NanoH2 (ESP32-H2) | Pushbutton on `G2` shown on the RGB LED — green while it feeds 3.3 V, yellow while open; no Zigbee |

## Tests

[`test/`](test/) holds host tests for the pure-logic parts of the sketches — no
board or embedded toolchain needed:

```
cd test && make
```

## Tools

[`tools/serial-capture.sh`](tools/serial-capture.sh) records a NanoH2's serial
console to `~/nanoh2-logs/`, with a timestamp on every line. Use it when the device
has to stay where it is, on its real parent router, with a notebook beside it and
no network. The timestamps let the log be lined up with the Zigbee2MQTT log
afterwards. It uses the arduino-cli bundled with the Arduino IDE flatpak. It refuses to start
while another program has the port open, typically the IDE's Serial Monitor, and
lists what holds it.

While the device sends nothing, the script prints a status line every 60 s, on
screen only. So "device quiet" can be told apart from "capture hung". The firmware
can be silent for minutes when the temperatures stay inside the deadband and force
push is off. `--reset` restarts the board first, so the boot log arrives within
seconds as proof that data gets through. The cost is one rejoin.

```
tools/serial-capture.sh                    # first /dev/ttyACM*
tools/serial-capture.sh --reset            # restart the board, capture from boot
tools/serial-capture.sh --wait             # cold boot: start, then power the board on
tools/serial-capture.sh /dev/ttyACM1
```

`--wait` is for a cold boot: start the script with the board off, then power it on.
The script waits for the port to appear and captures from the first line, banner
included. The board waits up to 2 s for a host before it starts printing.
