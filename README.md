# esp32

ESP32 sketches and firmware projects.

| Project | Target | Description |
| --- | --- | --- |
| [NanoH2_DS18B20_Zigbee](NanoH2_DS18B20_Zigbee/) | M5Stack NanoH2 (ESP32-H2) | Three DS18B20 sensors published over Zigbee, with factory-reset button and RGB link-state LED |

## Tests

[`test/`](test/) holds host tests for the pure-logic parts of the sketches — no
board or embedded toolchain needed:

```
cd test && make
```
