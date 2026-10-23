#!/bin/bash
# Flash a .bin to the WPM over USB with bossac.
# Usage: flash.sh <bin> [port] [--dry-run]
# Port detection: prefers the Studio H CDC port (VID 0x1209 PID 0xFBA0), which needs the Arduino
# 1200-baud touch (this ERASES the first flash row of the app) to enter the bootloader; else uses
# the MKR1000 bootloader port (VID 0x2341). The touch is only done when a real upload is about to run.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BOSSAC="$HOME/Library/Arduino15/packages/arduino/tools/bossac/1.7.0-arduino3/bossac"
# The bench Mac has another SAMD board on this port; never touch it. Override with WPM_EXCLUDE_PORT
# (set it to an unused path to exclude nothing).
EXCLUDE_PORT="${WPM_EXCLUDE_PORT:-/dev/cu.usbmodem192573201}"
[ -x "$BOSSAC" ] || { echo "error: bossac not found at $BOSSAC (install the arduino:samd core's bossac 1.7.0-arduino3 tool)" >&2; exit 1; }

BIN=""; PORT=""; DRY=0
for a in "$@"; do
  case "$a" in
    --dry-run) DRY=1 ;;
    *) if [ -z "$BIN" ]; then BIN="$a"; elif [ -z "$PORT" ]; then PORT="$a"; else echo "too many arguments" >&2; exit 2; fi ;;
  esac
done
[ -n "$BIN" ] && [ -f "$BIN" ] || { echo "usage: $0 <bin> [port] [--dry-run]" >&2; exit 2; }

# Print "<port> <vid> <pid>" for every USB serial port, excluding the protected one.
list_ports() {
  arduino-cli board list --format json | python3 -c '
import json, sys
d = json.load(sys.stdin)
d = d.get("detected_ports", d) if isinstance(d, dict) else d
for e in d:
    p = e.get("port", {}); pr = p.get("properties", {}) or {}
    if p.get("address", "").startswith("/dev/cu.usbmodem") and pr.get("vid"):
        print(p["address"], pr["vid"].lower(), pr.get("pid", "").lower())
' | grep -v "^$EXCLUDE_PORT " || true
}

find_port() {  # $1 = vid (lower-case 0x....)
  list_ports | awk -v v="$1" '$2 == v { print $1; exit }'
}

MODE=""
if [ -n "$PORT" ]; then
  [ "$PORT" != "$EXCLUDE_PORT" ] || { echo "refusing to use $EXCLUDE_PORT" >&2; exit 2; }
  if list_ports | grep -q "^$PORT 0x2341 "; then MODE=boot; else MODE=app; fi
else
  PORT="$(find_port 0x1209)"
  if [ -n "$PORT" ]; then MODE=app; else PORT="$(find_port 0x2341)"; MODE=boot; fi
  [ -n "$PORT" ] || { echo "no WPM port found" >&2; exit 1; }
fi

mkdir -p "$HERE/bench"
LOG="$HERE/bench/bossac-$(date +%Y%m%d-%H%M%S).log"

if [ "$DRY" = 1 ]; then
  echo "mode: $MODE (port $PORT)"
  [ "$MODE" = app ] && echo "would touch: stty -f $PORT 1200, then wait up to 10 s for the bootloader port"
  echo "would run: $BOSSAC -i -d --port=<bootloader port basename> -U true -i -e -w -v $BIN -R"
  echo "would log to: $LOG"
  exit 0
fi

if [ "$MODE" = app ]; then
  echo "1200-baud touch on $PORT"
  stty -f "$PORT" 1200 || true
  PORT=""
  for _ in $(seq 1 50); do
    sleep 0.2
    PORT="$(find_port 0x2341)"
    [ -n "$PORT" ] && break
  done
  [ -n "$PORT" ] || { echo "bootloader port did not appear within 10 s" >&2; exit 1; }
  sleep 0.5
fi

echo "bootloader port: $PORT"
{ echo "# $(date) bin=$BIN port=$PORT"; "$BOSSAC" -i -d --port="$(basename "$PORT")" -U true -i -e -w -v "$BIN" -R 2>&1; } | tee "$LOG"
echo "bossac log: $LOG"
