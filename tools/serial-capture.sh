#!/usr/bin/env bash
# Records a NanoH2's serial console to a timestamped log file, for runs where the
# device has to stay where it is (outside, on its real parent) and nobody can watch
# the console live. Every line is shown on screen and written to the file, with a
# timestamp, so the log can be matched against the Zigbee2MQTT log afterwards.
#
# Usage: tools/serial-capture.sh [--reset | --wait] [port]   (default: the first /dev/ttyACM*)
# Stop with Ctrl+C. Logs go to ~/nanoh2-logs/.
#
#   --reset  restarts the board just before the capture starts, so the boot log
#            (banner with the version, the EP list, the join, the first readings)
#            arrives within seconds - proof that data gets through. Costs a rejoin.
#   --wait   for a cold boot: start this first, then power the board on. Waits for
#            its serial port to appear and captures from the first line. The board
#            waits up to 2 s for a host to open the port (SERIAL_WAIT_MS), which is
#            long enough for this to catch the banner.
#
# The firmware is quiet between events: with force push off and temperatures inside
# the deadband, minutes can pass without a line. So while nothing arrives, a status
# line is shown every 60 s (on screen only, not in the log), which tells "device
# quiet" apart from "capture hung".
#
# Refuses to start while another program has the port open - the Arduino IDE's
# Serial Monitor, typically - and lists what holds it.
# Uses the arduino-cli and esptool that come with the Arduino IDE flatpak's setup.

set -euo pipefail

CLI=${ARDUINO_CLI:-/var/lib/flatpak/app/cc.arduino.IDE2/current/active/files/arduino-ide/resources/app/lib/backend/resources/arduino-cli}
ESPTOOL=$(ls "$HOME"/.arduino15/packages/m5stack/tools/esptool_py/*/esptool 2>/dev/null | tail -n 1 || true)
LOG_DIR="$HOME/nanoh2-logs"
IDLE_NOTICE_S=${IDLE_NOTICE_S:-60}

reset=0
wait=0
port=""
for arg in "$@"; do
  case "$arg" in
    --reset) reset=1 ;;
    --wait) wait=1 ;;
    -h | --help)
      sed -n '2,/^$/p' "$0" | sed '/^$/d; s/^# \{0,1\}//'
      exit 0
      ;;
    *) port="$arg" ;;
  esac
done

if ((reset && wait)); then
  echo "--reset and --wait exclude each other: --wait is for a board that is still off" >&2
  exit 1
fi

# The port the board shows up as: the one asked for, else the first /dev/ttyACM*.
find_port() {
  if [[ -n "$port" ]]; then
    [[ -e "$port" ]] && echo "$port"
  else
    ls /dev/ttyACM* 2>/dev/null | head -n 1 || true
  fi
}

waitStart=""
if ((wait)) && [[ -z "$(find_port)" ]]; then
  waitStart=$(date --iso-8601=seconds)
  echo "Waiting for ${port:-a /dev/ttyACM* port} - power the board on now (Ctrl+C to give up)"
  while [[ -z "$(find_port)" ]]; do
    sleep 0.05
  done
  # The device node exists a moment before its permissions are set; opening it in
  # that moment fails. A tenth of a second is well inside the board's 2 s host wait.
  sleep 0.1
fi

port=$(find_port)
if [[ -z "$port" ]]; then
  echo "No serial port found - is the NanoH2 plugged in? (--wait waits for it)" >&2
  exit 1
fi

# Something else holding the port - the IDE's Serial Monitor, an upload still
# running, a second capture - would either keep this one from opening it or share
# the incoming bytes with it, and then both logs have holes. So look first: every
# open file of every process of this user is a link under /proc/<pid>/fd, the
# flatpak IDE's included, since its sandbox shares /dev with the host. Resolved
# with readlink -f, so a /dev/serial/by-id/... name is matched as the device it
# points to.
#
# One find for the lot: a readlink per file descriptor is a process per open file,
# and with a browser or the IDE running that is ten thousand of them - 17 s.
target=$(readlink -f "$port")
holders=()
while IFS= read -r fd; do
  pid=${fd#/proc/}
  pid=${pid%%/*}
  [[ " ${holders[*]} " == *" $pid "* ]] || holders+=("$pid")
done < <(find /proc/[0-9]*/fd -maxdepth 1 -lname "$target" 2>/dev/null)
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

# The host clock and zone go first, so the timestamps below can be lined up with
# the Zigbee2MQTT log even if the two machines' clocks disagree.
echo "# capture start $(date --iso-8601=seconds) on $(hostname), port $port" >"$log"
if [[ -n "$waitStart" ]]; then
  # When the wait began, so the time to the port appearing - the board powering up
  # and enumerating - can be read off the log as well.
  echo "# waited for the port from $waitStart (--wait)" >>"$log"
fi

if ((reset)); then
  if [[ -z "$ESPTOOL" ]]; then
    echo "--reset: esptool not found under ~/.arduino15/packages/m5stack/tools/esptool_py" >&2
    exit 1
  fi
  echo "Restarting the board on $port ..."
  # read-mac is the cheapest thing esptool can do with the chip; --after hard-reset
  # is the point of the call: it restarts the board into the sketch when done.
  if ! "$ESPTOOL" --chip esp32h2 --port "$port" --after hard-reset read-mac >/dev/null; then
    echo "--reset: esptool could not reach the board" >&2
    exit 1
  fi
  echo "# board restarted by --reset $(date --iso-8601=seconds)" >>"$log"
  # The USB device goes away while the chip restarts. Wait for it to come back.
  for _ in $(seq 1 50); do
    [[ -e "$port" ]] && break
    sleep 0.1
  done
fi

echo "Capturing $port to $log - Ctrl+C to stop"

# Status while the device is silent. Watches the log's line count, so it only
# speaks up when nothing new has been written for IDLE_NOTICE_S seconds.
(
  last=$(wc -l <"$log")
  idle=0
  while sleep "$IDLE_NOTICE_S"; do
    now=$(wc -l <"$log")
    if ((now == last)); then
      idle=$((idle + IDLE_NOTICE_S))
      echo "[$(date +%H:%M:%S)] capturing, no data from the device for ${idle} s" >&2
    else
      idle=0
      last=$now
    fi
  done
) &
watchdog=$!
trap 'kill "$watchdog" 2>/dev/null' EXIT

# Ctrl+C is meant for the monitor. The script lives on long enough to say how
# much it caught.
trap ':' INT
status=0
"$CLI" --config-file "$HOME/.arduinoIDE/arduino-cli.yaml" monitor -p "$port" \
  --config baudrate=115200 --timestamp --quiet | tee -a "$log" || status=$?

lines=$(grep -vc '^#' "$log" || true)  # what the board sent, without the script's own # lines
echo "Capture ended (exit $status), $lines line(s) in $log" >&2
