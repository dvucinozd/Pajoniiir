#!/bin/bash
# HIL helper for the JC1060 console and OTA files (Linux).
# Adapted from the Pajoniiir-fork-ref tool; no Docker, no TCP console.
#
#   hil.sh daemon       : start the permanent UART0 capture (auto port)
#   hil.sh stop         : stop the capture
#   hil.sh tail N PAT   : last N log lines, optional case-insensitive grep
#   hil.sh status       : capture state, log size, last version/boot lines
#   hil.sh serve        : serve the build directory over HTTP (port 8080)
#
# Environment: HIL_PORT (default first /dev/ttyUSB*), HIL_BAUD (115200),
# HIL_LOG (~/.cache/pajoniiir-hil/serial_all.log), HIL_BUILD_DIR
# (firmware/main-deck-jc1060/build), HIL_HTTP_PORT (8080).
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
STATE=${XDG_CACHE_HOME:-$HOME/.cache}/pajoniiir-hil
LOG=${HIL_LOG:-$STATE/serial_all.log}
BAUD=${HIL_BAUD:-115200}
BUILD_DIR=${HIL_BUILD_DIR:-$ROOT/firmware/main-deck-jc1060/build}
HTTP_PORT=${HIL_HTTP_PORT:-8080}
PIDFILE=$STATE/capture.pid
mkdir -p "$STATE"

serial_port() { [ -n "${HIL_PORT:-}" ] && echo "$HIL_PORT" || ls /dev/ttyUSB* 2>/dev/null | head -1; }
alive() { [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; }
host_ip() { ip -4 -o addr show scope global 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -1; }

case "${1:-}" in
  daemon)
    alive && { echo "capture already running (pid $(cat "$PIDFILE"))"; exit 0; }
    p=$(serial_port); [ -z "$p" ] && { echo "no serial port (set HIL_PORT)"; exit 1; }
    nohup python3 "$ROOT/tools/hil/hil_capture.py" "$p" "$LOG" "$BAUD" >/dev/null 2>&1 &
    echo $! > "$PIDFILE"
    echo "capture started on $p -> $LOG (pid $!)" ;;
  stop)
    alive && kill "$(cat "$PIDFILE")" && rm -f "$PIDFILE" && echo "stopped" || echo "not running" ;;
  tail)
    N="${2:-40}"; PAT="${3:-}"
    if [ -n "$PAT" ]; then
      tail -n "$N" "$LOG" 2>/dev/null | tr -d '\000' | grep -iE "$PAT" | tail -n 30
    else
      tail -n "$N" "$LOG" 2>/dev/null | tr -d '\000'
    fi ;;
  status)
    alive && echo "capture: alive (pid $(cat "$PIDFILE"))" || echo "capture: NOT running (hil.sh daemon)"
    echo "port: $(serial_port)  log: $(stat -c %s "$LOG" 2>/dev/null || echo 0) bytes ($LOG)"
    echo "-- version / reset reason:"
    bash "$0" tail 4000 'version|reset reason' | tail -n 3
    echo "-- USB / UAC / OTA events:"
    bash "$0" tail 4000 'New device|mounted|CONNECTED|reject|MIDI|UAC|usb_audio|OTA' | tail -n 8 ;;
  serve)
    [ -d "$BUILD_DIR" ] || { echo "no build dir: $BUILD_DIR"; exit 1; }
    ip=$(host_ip)
    echo "serving $BUILD_DIR on http://$ip:$HTTP_PORT/ (Ctrl-C to stop)"
    echo "image: http://$ip:$HTTP_PORT/main-deck-jc1060.bin"
    exec python3 -m http.server "$HTTP_PORT" --bind 0.0.0.0 --directory "$BUILD_DIR" ;;
  *) echo "usage: hil.sh {daemon|stop|tail N PAT|status|serve}" ;;
esac
