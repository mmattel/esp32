#!/usr/bin/env bash
# Records a NanoH2's serial console to a timestamped log file, for runs where the
# device has to stay where it is (outside, on its real parent) and nobody can watch
# the console live. Every line gets a timestamp, so the log can be matched against
# the Zigbee2MQTT log afterwards.
#
# Usage: tools/serial-capture.sh [port]      (default: the first /dev/ttyACM*)
# Stop with Ctrl+C. Logs go to ~/nanoh2-logs/.
#
# Close the Arduino IDE's Serial Monitor first: only one program can hold the port.
# Uses the arduino-cli bundled with the Arduino IDE flatpak.

set -euo pipefail

CLI=/var/lib/flatpak/app/cc.arduino.IDE2/current/active/files/arduino-ide/resources/app/lib/backend/resources/arduino-cli
LOG_DIR="$HOME/nanoh2-logs"

port="${1:-}"
if [[ -z "$port" ]]; then
  port=$(ls /dev/ttyACM* 2>/dev/null | head -n 1 || true)
fi
if [[ -z "$port" || ! -e "$port" ]]; then
  echo "No serial port found - is the NanoH2 plugged in?" >&2
  exit 1
fi

mkdir -p "$LOG_DIR"
log="$LOG_DIR/nanoh2-$(date +%Y%m%d-%H%M%S).log"
echo "Capturing $port to $log - Ctrl+C to stop"

# The host clock and zone go first, so the timestamps below can be lined up with
# the Zigbee2MQTT log even if the two machines' clocks disagree.
echo "# capture start $(date --iso-8601=seconds) on $(hostname), port $port" >"$log"

"$CLI" --config-file "$HOME/.arduinoIDE/arduino-cli.yaml" monitor -p "$port" \
  --config baudrate=115200 --timestamp --quiet | tee -a "$log"
