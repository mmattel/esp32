// Zigbee2MQTT external definition for the NanoH2 WaterFlow Zigbee sketch.
//
// Z2M generated the numeric entries (device page -> Dev console ->
// Generate external definition).  Two entries were added by hand - both
// m.text() calls - for attribute 0xF000 on endpoint 15 (mirrored console
// line) and endpoint 16 (firmware version string).  Z2M cannot generate
// these because a generated definition can express a number but not a
// custom character-string attribute.
//
// An external definition REPLACES the generated one entirely.  If the
// firmware gains or loses an endpoint this file must be regenerated from
// Z2M and the two m.text() entries re-added by hand, then the new copy
// installed in Z2M's external_converters/ directory.
//
// To install: copy this file to <z2m-data>/external_converters/ and
// restart Z2M (or reload external converters).
//
// IMPORTANT: two copies of this file exist.  Keep them in sync:
//   - sketch folder (this file)
//   - <z2m-data>/external_converters/nanoh2-waterflow.mjs

import * as m from 'zigbee-herdsman-converters/lib/modernExtend';

export default {
    zigbeeModel: ['NanoH2-WaterFlow'],
    model: 'NanoH2-WaterFlow',
    vendor: 'M5Stack',
    description: 'Hall-effect water flow sensor over Zigbee with settings, totals, link quality, console mirror and firmware version',
    extend: [
        // Fixed block 10-16, then flow measurements 20-24.
        m.deviceEndpoints({
            endpoints: {10: 10, 11: 11, 12: 12, 13: 13, 14: 14, 15: 15, 16: 16, 20: 20, 21: 21, 22: 22, 23: 23, 24: 24},
        }),
        m.identify(),

        // ----------------------------------------------------------------
        // Writable settings (Analog Output, endpoints 10-12)
        // ----------------------------------------------------------------

        // EP 10: impulses per litre - sensor calibration constant.
        // A YF-S201 type sensor is typically ~450; verify against a known
        // reference volume and adjust until the totals agree.
        m.numeric({
            name: 'impulses_per_litre',
            label: 'Impulses per litre',
            valueMin: 0,
            valueMax: 5000,
            valueStep: 0.001,
            cluster: 'genAnalogOutput',
            attribute: 'presentValue',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Sensor calibration: pulse count per litre of water',
            access: 'ALL',
            endpointNames: ['10'],
        }),

        // EP 11: NVS writeback time.  The running total (EP 22) is saved to
        // flash this many seconds after the last pulse.  1-20 s range.
        m.numeric({
            name: 'nvs_writeback_time_s',
            label: 'NVS writeback time (s)',
            valueMin: 1,
            valueMax: 20,
            valueStep: 1,
            cluster: 'genAnalogOutput',
            attribute: 'presentValue',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Seconds of sensor inactivity before the total is saved to flash',
            access: 'ALL',
            endpointNames: ['11'],
            unit: 's',
        }),

        // EP 12: total start value.  Writing a value here sets the running
        // total (EP 22) to that value immediately, saves it to flash, and
        // resets EP 24 (total since last reset) to 0.  Write 0 to reset.
        m.numeric({
            name: 'total_start_value_l',
            label: 'Total start value (L)',
            valueMin: 0,
            valueMax: 9999999,
            valueStep: 0.001,
            cluster: 'genAnalogOutput',
            attribute: 'presentValue',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Sets the running total to this value and resets the "since last reset" counter',
            access: 'ALL',
            endpointNames: ['12'],
            unit: 'L',
        }),

        // ----------------------------------------------------------------
        // Diagnostics (Analog Input, endpoints 13-16)
        // ----------------------------------------------------------------

        m.numeric({
            name: 'parent_link_lqi',
            label: 'Parent link LQI',
            valueMin: 0,
            valueMax: 255,
            valueStep: 1,
            cluster: 'genAnalogInput',
            attribute: 'presentValue',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Link quality index to the parent (device-side view)',
            access: 'STATE_GET',
            endpointNames: ['13'],
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
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Signal strength to the parent in dBm (device-side view)',
            access: 'STATE_GET',
            endpointNames: ['14'],
            unit: 'dBm',
            entityCategory: 'diagnostic',
        }),
        // EP 15, presentValue: sequence number of the mirrored console line.
        m.numeric({
            name: 'mirror_line_count',
            label: 'Mirror line count',
            valueMin: 0,
            valueMax: 65535,
            valueStep: 1,
            cluster: 'genAnalogInput',
            attribute: 'presentValue',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Count of console events mirrored over the air; a gap means lines printed while off the air',
            access: 'STATE_GET',
            endpointNames: ['15'],
            entityCategory: 'diagnostic',
        }),
        // EP 16, presentValue: firmware version as a sortable integer (1.0.0 -> 10000).
        m.numeric({
            name: 'firmware_version_number',
            label: 'Firmware version number',
            valueMin: 0,
            valueMax: 999999,
            valueStep: 1,
            cluster: 'genAnalogInput',
            attribute: 'presentValue',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Firmware version as a sortable integer (major*10000 + minor*100 + patch)',
            access: 'STATE_GET',
            endpointNames: ['16'],
            entityCategory: 'diagnostic',
        }),

        // ----------------------------------------------------------------
        // Flow measurements (Analog Input, endpoints 20-24)
        // ----------------------------------------------------------------

        // EP 20: flow rate in litres per minute.
        m.numeric({
            name: 'flow_rate_l_per_min',
            label: 'Flow rate (L/min)',
            valueMin: 0,
            valueMax: 9999,
            valueStep: 0.001,
            cluster: 'genAnalogInput',
            attribute: 'presentValue',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Current flow rate in litres per minute',
            access: 'STATE_GET',
            endpointNames: ['20'],
            unit: 'L/min',
        }),
        // EP 21: flow rate in litres per second.
        m.numeric({
            name: 'flow_rate_l_per_s',
            label: 'Flow rate (L/s)',
            valueMin: 0,
            valueMax: 999,
            valueStep: 0.0001,
            cluster: 'genAnalogInput',
            attribute: 'presentValue',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Current flow rate in litres per second',
            access: 'STATE_GET',
            endpointNames: ['21'],
            unit: 'L/s',
        }),
        // EP 22: running total in litres, saved to flash on inactivity.
        m.numeric({
            name: 'total_consumption_l',
            label: 'Total consumption (L)',
            valueMin: 0,
            valueMax: 9999999,
            valueStep: 0.001,
            cluster: 'genAnalogInput',
            attribute: 'presentValue',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Cumulative water consumption in litres (persisted in flash)',
            access: 'STATE_GET',
            endpointNames: ['22'],
            unit: 'L',
        }),
        // EP 23: running total in cubic metres, derived from EP 22 / 1000.
        m.numeric({
            name: 'total_consumption_m3',
            label: 'Total consumption (m³)',
            valueMin: 0,
            valueMax: 9999.999,
            valueStep: 0.000001,
            cluster: 'genAnalogInput',
            attribute: 'presentValue',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Cumulative water consumption in cubic metres',
            access: 'STATE_GET',
            endpointNames: ['23'],
            unit: 'm³',
        }),
        // EP 24: litres since the start-value setting was last written.
        // Resets to 0 on each write to EP 12, and on every reboot (not persisted).
        m.numeric({
            name: 'total_since_last_reset_l',
            label: 'Total since last reset (L)',
            valueMin: 0,
            valueMax: 9999999,
            valueStep: 0.001,
            cluster: 'genAnalogInput',
            attribute: 'presentValue',
            reporting: {min: 'MIN', max: 'MAX', change: 1},
            description: 'Litres since the total start value was last written (resets on write to EP 12 or on reboot)',
            access: 'STATE_GET',
            endpointNames: ['24'],
            unit: 'L',
        }),

        // ----------------------------------------------------------------
        // Text attributes added by hand (m.text) - Z2M cannot generate these
        // ----------------------------------------------------------------

        // EP 15, attribute 0xF000: the mirrored console line as a ZCL char string.
        // Space-padded to 64 characters; trim() it in automations.
        m.text({
            name: 'console_mirror',
            cluster: 'genAnalogInput',
            attribute: {ID: 0xf000, type: 0x42},
            description: 'Last console line worth an event, space padded to 64 chars',
            access: 'STATE_GET',
            endpointName: '15',
            entityCategory: 'diagnostic',
        }),
        // EP 16, attribute 0xF000: firmware version string (e.g. "1.0.0").
        m.text({
            name: 'firmware_version',
            cluster: 'genAnalogInput',
            attribute: {ID: 0xf000, type: 0x42},
            description: 'Firmware version string this device is running',
            access: 'STATE_GET',
            endpointName: '16',
            entityCategory: 'diagnostic',
        }),
    ],
};
