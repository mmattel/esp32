// Zigbee2MQTT external definition for the NanoH2 DS18B20 Zigbee sketch.
//
// Z2M generated the body of this (device page -> Dev console -> Generate external
// definition) for a build with MAX_DS18B20_SENSORS = 3. Two things were added by hand,
// both m.text() entries and both for an attribute 0xF000: the mirrored console line on
// endpoint 14, and the firmware version string on endpoint 16. Z2M cannot generate
// either, because a generated definition can express a number but not a string.
//
// The endpoint 14 counter is named mirror_line_count by the generator itself, from the
// Analog Input cluster's description attribute, which the sketch sets to "Mirror line
// count" for exactly that reason. So it needs no renaming here, and the name
// console_mirror stays free for the text.
//
// An external definition REPLACES the generated one, so anything missing here
// disappears from the device page - and an endpoint the firmware gained stays
// invisible however often the device is re-interviewed or re-paired, which is the one
// failure that looks like a firmware bug and is not. So regenerate and re-add the
// m.text() entry whenever the endpoint list changes, which is not only a different
// MAX_DS18B20_SENSORS: a setting or a diagnostic endpoint added or switched off does
// it too. Endpoint 15, the temperature correction, is one that did.
//
// How to install this in Zigbee2MQTT, and why: see "Adding the external converter" in
// README.md. This copy is the one kept with the sketch; Zigbee2MQTT holds its own copy
// under external_converters/, so a change here has to be carried over to it.

// Converter version: 1.4
//
// Changelog (newest first):
//   1.4  2026-10-10  Fix temperature reporting: replace reporting: false with
//                    explicit config so Z2M creates the msTemperatureMeasurement
//                    binding — without it reportTemperature() delivered to nobody
//   1.3  2026-10-09  Add EP 17: force_push_min (force push interval, 0-120 min)
//   1.2  2026-10-06  Add README link to description for Info tab clickthrough
//   1.1  2026-10-06  Remove m.identify(); remove reporting: from all STATE_GET
//                    (genAnalogInput) endpoints to prevent Z-Stack coordinator
//                    overload on join — see "How the Converter Is Written" in README
//   1.0  (initial)   Generated from Z2M Dev console; m.text() entries added by
//                    hand for console mirror (EP 14) and firmware version (EP 16)

import * as m from 'zigbee-herdsman-converters/lib/modernExtend';

export default {
    zigbeeModel: ['NanoH2-DS18B20'],
    model: 'NanoH2-DS18B20',
    vendor: 'M5Stack',
    description: '[DS18B20 temperatures over Zigbee, with settings, link quality, a console mirror and its firmware version](https://github.com/mmattel/esp32/blob/main/NanoH2_DS18B20_Zigbee/README.md)',
    extend: [
        // 10 to 16 are fixed. Everything from 20 up is one endpoint per configured
        // sensor slot, so this list and the m.temperature() one below have to hold
        // exactly MAX_DS18B20_SENSORS of them - see the comment there.
        m.deviceEndpoints({
            endpoints: {10: 10, 11: 11, 12: 12, 13: 13, 14: 14, 15: 15, 16: 16, 17: 17, 20: 20, 21: 21, 22: 22},
        }),
        m.numeric({
            name: 'reading_interval_s',
            label: 'Reading interval (s)',
            valueMin: 10,
            valueMax: 3600,
            valueStep: 1,
            cluster: 'genAnalogOutput',
            attribute: 'presentValue',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Analog Output Reading interval (s) on endpoint 10',
            access: 'ALL',
            endpointNames: ['10'],
            unit: 's',
        }),
        m.numeric({
            name: 'reporting_delta_c',
            label: 'Reporting delta (C)',
            valueMin: 0,
            valueMax: 20,
            valueStep: 0.25,
            cluster: 'genAnalogOutput',
            attribute: 'presentValue',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Analog Output Reporting delta (C) on endpoint 11',
            access: 'ALL',
            endpointNames: ['11'],
            unit: '°C',
        }),
        // Endpoint 15, out of order on purpose: the device registers this setting with
        // the two above it, so that is where a generated definition puts it, and the
        // number is above the mirror's only because it was added after 10 to 14 were in
        // the field - see EP_CONFIG_CORRECTION in config.h. It is one value for every
        // sensor on the device, not one per slot; ±5 °C in quarter steps, 0 for none.
        m.numeric({
            name: 'temperature_correction_c',
            label: 'Temperature correction (C)',
            valueMin: -5,
            valueMax: 5,
            valueStep: 0.25,
            cluster: 'genAnalogOutput',
            attribute: 'presentValue',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Analog Output Temperature correction (C) on endpoint 15',
            access: 'ALL',
            endpointNames: ['15'],
            unit: '°C',
        }),
        // Endpoint 17: force push interval. When non-zero, all temperatures are
        // re-published every N minutes regardless of the reporting delta. 0 disables.
        m.numeric({
            name: 'force_push_min',
            label: 'Force push interval (min)',
            valueMin: 0,
            valueMax: 120,
            valueStep: 1,
            cluster: 'genAnalogOutput',
            attribute: 'presentValue',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Analog Output Force push interval (min) on endpoint 17 — 0 disables; non-zero forces a re-publish of all temperatures every N minutes regardless of the reporting delta',
            access: 'ALL',
            endpointNames: ['17'],
            unit: 'min',
        }),
        m.numeric({
            name: 'parent_link_lqi',
            label: 'Parent link LQI',
            valueMin: 0,
            valueMax: 255,
            valueStep: 1,
            cluster: 'genAnalogInput',
            attribute: 'presentValue',
            description: 'Analog Input Parent link LQI on endpoint 12',
            access: 'STATE_GET',
            endpointNames: ['12'],
            entityCategory: 'diagnostic',
        }),
        m.numeric({
            name: 'parent_link_rssi',
            label: 'Parent link RSSI',
            valueMin: -128,
            valueMax: 0,
            valueStep: 1,
            cluster: 'genAnalogInput',
            attribute: 'presentValue',
            description: 'Analog Input Parent link RSSI on endpoint 13',
            access: 'STATE_GET',
            endpointNames: ['13'],
            unit: 'dBm',
            entityCategory: 'diagnostic',
        }),
        // Endpoint 14, presentValue: the sequence number of the mirrored line. A gap
        // in it is honest - it says lines were printed while the device was off the air.
        m.numeric({
            name: 'mirror_line_count',
            label: 'Mirror line count',
            valueMin: 0,
            valueMax: 65535,
            valueStep: 1,
            cluster: 'genAnalogInput',
            attribute: 'presentValue',
            description: 'Analog Input Mirror line count on endpoint 14',
            access: 'STATE_GET',
            endpointNames: ['14'],
            entityCategory: 'diagnostic',
        }),
        // Endpoint 16, presentValue: the firmware version as one number that sorts,
        // 2.0.0 -> 20000, two digits each for the minor and the patch. This is the half
        // an automation can put a condition on; the string beside it is the readable
        // one, and Z2M's own "Firmware build ID" on the device page is a third copy of
        // the same version, read from the Basic cluster during the interview.
        //
        // The device sends both after every join - which is the only moment the answer
        // can have changed, since changing it means flashing. No reporting entry: the
        // device reports this attribute itself after every join.
        m.numeric({
            name: 'firmware_version_number',
            label: 'Firmware version number',
            valueMin: 0,
            valueMax: 999999,
            valueStep: 1,
            cluster: 'genAnalogInput',
            attribute: 'presentValue',
            description: 'Analog Input Firmware version number on endpoint 16',
            access: 'STATE_GET',
            endpointNames: ['16'],
            entityCategory: 'diagnostic',
        }),
        // Endpoints 20, 21 and 22: one slot each, whether or not a sensor is in it. A
        // slot with nothing connected stays N/A rather than reporting a wrong value.
        //
        // MUST MATCH MAX_DS18B20_SENSORS IN config.h, which is 3 for this list. The
        // sketch creates one temperature endpoint per configured slot, numbered up from
        // EP_TEMP_BASE (20), and nothing on the device side adapts to a converter that
        // disagrees: an endpoint listed here that the firmware does not have is an
        // expose that stays N/A for good, plus a binding Z2M logs as failed during
        // configure - and one left out hides a sensor that is really reporting.
        // reporting: is required here — Z2M uses the reporting entry to also send a bind
        // command for msTemperatureMeasurement. Without the bind, the device has no
        // binding table entry and every reportTemperature() call is silently discarded.
        // The firmware manages its own deadband (reporting_delta_c, EP 11) and force push
        // (force_push_min, EP 17) entirely in software; these thresholds are fallbacks only.
        m.temperature({endpointNames: ['20', '21', '22'], reporting: {min: 10, max: 3600, change: 25}}),
        // Endpoint 14, attribute 0xF000: the mirrored line itself, a ZCL character
        // string (type 0x42). Note endpointName, singular, where the numerics above
        // take endpointNames - m.text() differs from m.numeric() there.
        //
        // No reporting entry on purpose: the device reports this attribute by itself
        // on every new line and once an hour as a heartbeat, so there is nothing for
        // Z2M to configure. The value arrives space padded to 64 characters.
        m.text({
            name: 'console_mirror',
            cluster: 'genAnalogInput',
            attribute: {ID: 0xf000, type: 0x42},
            description: 'Last console line worth an event, space padded',
            access: 'STATE_GET',
            endpointName: '14',
            entityCategory: 'diagnostic',
        }),
        // Endpoint 16, attribute 0xF000: the firmware version as it is printed in the
        // boot banner. The same attribute number as the mirrored line above, on a
        // different endpoint - attributes are per endpoint and per cluster, so there is
        // nothing to keep apart. Not space padded, unlike the line: the version is fixed
        // at compile time, so the attribute is created at exactly its length.
        //
        // No reporting entry, for the same reason as the mirrored line: the device
        // reports this attribute itself, here after every join.
        m.text({
            name: 'firmware_version',
            cluster: 'genAnalogInput',
            attribute: {ID: 0xf000, type: 0x42},
            description: 'Firmware version this device is running',
            access: 'STATE_GET',
            endpointName: '16',
            entityCategory: 'diagnostic',
        }),
    ],
};
