# esp32

ESP32 sketches and firmware projects.

| Project | Target | Description |
| --- | --- | --- |
| [NanoH2_DS18B20_Zigbee](NanoH2_DS18B20_Zigbee/) | M5Stack NanoH2 (ESP32-H2) | DS18B20 sensors published over Zigbee, three by default, with factory-reset button and RGB link-state LED |
| [NanoH2_Button_LED](NanoH2_Button_LED/) | M5Stack NanoH2 (ESP32-H2) | Pushbutton on `G1` shown on the RGB LED — green while it feeds 3.3 V, yellow while open; no Zigbee |

## Tests

[`test/`](test/) holds host tests for the pure-logic parts of the sketches — no
board or embedded toolchain needed:

```
cd test && make
```
