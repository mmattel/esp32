#!/usr/bin/env bash
# Records a NanoH2's serial console to a timestamped log file, for runs where the
# device has to stay where it is (outside, on its real parent) and nobody can watch
# the console live. Every line gets a timestamp, so the log can be matched against
# the Zigbee2MQTT log afterwards.
#
# Usage: tools/serial-capture.sh [port]      (default: the first /dev/ttyACM*)
# Stop with Ctrl+C. Logs go to ~/nanoh2-logs/.
#
# Refuses to start while another program has the port open - the Arduino IDE's
# Serial Monitor, typically - and lists what holds it.
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

# Something else holding the port - the IDE's Serial Monitor, an upload still
# running, a second capture - would either keep this one from opening it or share
# the incoming bytes with it, and then both logs have holes. So look first: every
# open file of every process of this user is a link under /proc/<pid>/fd, the
# flatpak IDE's included, since its sandbox shares /dev with the host. Resolved
# with readlink -f, so a /dev/serial/by-id/... name is matched as the device it
# points to.
target=$(readlink -f "$port")
holders=()
for fd in /proc/[0-9]*/fd/*; do
  [[ "$(readlink "$fd" 2>/dev/null)" == "$target" ]] || continue
  pid=${fd#/proc/}
  pid=${pid%%/*}
  [[ " ${holders[*]} " == *" $pid "* ]] || holders+=("$pid")
done
if ((${#holders[@]} > 0)); then
  echo "$port is already open in another program:" >&2
  for pid in "${holders[@]}"; do
    cmd=$(tr '\0' ' ' <"/proc/$pid/cmdline" 2>/dev/null | cut -c1-100)
    echo "  PID $pid: ${cmd:-?}" >&2
  done
  echo "Close the Arduino IDE's Serial Monitor (or whatever is listed) and run this again." >&2
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
