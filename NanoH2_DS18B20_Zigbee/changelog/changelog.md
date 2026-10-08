# Changelog — NanoH2 DS18B20 Zigbee

Entries are in reverse order — newest first.

---

## 2.0.3

**Fixed:** post-join ZCL burst could saturate the Z-Stack coordinator.

`onZigbeeConnected()` sent all pending endpoint reports back-to-back (settings ×3,
firmware version, slot summary — roughly five in under 1 ms), which together with the
flag-driven temperature and link reports that follow in the next loop passes could
saturate the Z-Stack coordinator's ZNP serial queue.  When saturated, the coordinator
could not answer availability pings from other devices, causing Z2M to mark unrelated
devices — range extenders first, then anything routing through them — as offline.

`POST_JOIN_REPORT_DELAY_MS` (150 ms, in `config.h`) is now inserted between each
explicit `publish()` call in `onZigbeeConnected()`, spreading the burst over ~750 ms.

A matching `advanced: adapter_concurrent: 1 / adapter_delay: 100` in Z2M's
`configuration.yaml` provides defence-in-depth at the coordinator side; see
"Coordinator Throttling" in README.md.

No re-pair needed — timing change only; no endpoint or expose changes.

---

## 2.0.2 — converter only, no reflash required

**Fixed:** three expose names in `nanoh2-ds18b20.mjs` contained parentheses
(`reading_interval_(s)`, `reporting_delta_(c)`, `temperature_correction_(c)`).
Home Assistant rejects MQTT discovery topics that contain `(` or `)`, so those three
entities were never registered in HA. Parentheses replaced with underscores:
`reading_interval_s`, `reporting_delta_c`, `temperature_correction_c`.

Update both copies of the converter (sketch folder and Z2M `external_converters/`),
then re-interview the device in Z2M so the corrected entity names are published.
No firmware change; the ESP32 does not need to be reflashed.

---

## 2.0.1

Initial stable release. Firmware and converter shipped together and work as designed:
three DS18B20 temperature sensors, configurable reading interval and reporting delta,
temperature correction per device, LQI/RSSI link quality, console mirror, and firmware
version endpoint. No known issues at release.
