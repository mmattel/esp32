// Zigbee2MQTT external definition for the NanoH2 DS18B20 Zigbee sketch.
//
// Z2M generated the body of this (device page -> Dev console -> Generate external
// definition) for a build with MAX_DS18B20_SENSORS = 3. Exactly one thing was added
// by hand: the m.text() entry at the end, for attribute 0xF000 on endpoint 14 - the
// mirrored console line. Z2M cannot generate that, because a generated definition can
// express a number but not a string.
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

import * as m from 'zigbee-herdsman-converters/lib/modernExtend';

export default {
    zigbeeModel: ['NanoH2-DS18B20'],
    model: 'NanoH2-DS18B20',
    vendor: 'M5Stack',
    description: 'DS18B20 temperatures over Zigbee, with settings, link quality and a console mirror',
    extend: [
        // 10 to 15 are fixed. Everything from 20 up is one endpoint per configured
        // sensor slot, so this list and the m.temperature() one below have to hold
        // exactly MAX_DS18B20_SENSORS of them - see the comment there.
        m.deviceEndpoints({endpoints: {10: 10, 11: 11, 12: 12, 13: 13, 14: 14, 15: 15, 20: 20, 21: 21, 22: 22}}),
        m.identify(),
        m.numeric({
            name: 'reading_interval_(s)',
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
            name: 'reporting_delta_(c)',
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
            name: 'temperature_correction_(c)',
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
        m.numeric({
            name: 'parent_link_lqi',
            label: 'Parent link LQI',
            valueMin: 0,
            valueMax: 255,
            valueStep: 1,
            cluster: 'genAnalogInput',
            attribute: 'presentValue',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Analog Input Parent link LQI on endpoint 12',
            access: 'STATE_GET',
            endpointNames: ['12'],
        }),
        m.numeric({
            name: 'parent_link_rssi',
            label: 'Parent link RSSI',
            valueMin: -128,
            valueMax: 0,
            valueStep: 1,
            cluster: 'genAnalogInput',
            attribute: 'presentValue',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Analog Input Parent link RSSI on endpoint 13',
            access: 'STATE_GET',
            endpointNames: ['13'],
            unit: 'dBm',
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
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Analog Input Mirror line count on endpoint 14',
            access: 'STATE_GET',
            endpointNames: ['14'],
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
        m.temperature({endpointNames: ['20', '21', '22']}),
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
    ],
};
