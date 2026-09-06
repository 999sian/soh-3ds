#!/usr/bin/env bash
# Like run-and-shoot-full.sh but records the emulator window with ffmpeg
# (x11grab) from t0 for DUR seconds at FPS, for frame-to-frame flicker checks.
#   ROM=build-3ds-mk/soh_3ds.3dsx ./scripts/run-and-record.sh <tag> <t0> <dur> [fps]
set -euo pipefail
TAG="${1:-rec}"; T0="${2:-95}"; DUR="${3:-20}"; FPS="${4:-20}"
PRIV=/var/tmp/soh3ds-priv
OUT=/var/tmp/shots
APP=${APP:-/tmp/azahar.AppImage}
ROM=${ROM:-build-3ds-mk/soh_3ds.3dsx}
export TMPDIR=/var/tmp DISPLAY=${DISPLAY:-:1}
rm -rf "$PRIV/data/azahar-emu/log"; mkdir -p "$PRIV/data/azahar-emu/log" "$OUT"
XDG_DATA_HOME="$PRIV/data" XDG_CONFIG_HOME="$PRIV/config" QT_QPA_PLATFORM=xcb \
  setsid "$APP" "$ROM" > /var/tmp/azahar.stdout 2>&1 &
find_window() {
  for id in $(xprop -root _NET_CLIENT_LIST 2>/dev/null | sed 's/.*# //; s/,//g'); do
    p=$(xprop -id "$id" _NET_WM_PID 2>/dev/null | awk '{print $NF}'); [ -n "${p:-}" ] || continue
    if tr '\0' ' ' < "/proc/$p/cmdline" 2>/dev/null | grep -q 'build-3ds-mk'; then echo "$id"; return 0; fi
  done; return 1
}
WIN=""; for _ in $(seq 1 60); do if WIN=$(find_window); then break; fi; sleep 1; done
[ -n "$WIN" ] || { echo "emulator window not found" >&2; exit 1; }
while [ "$SECONDS" -lt "$T0" ]; do sleep 1; done
ffmpeg -loglevel error -y -f x11grab -framerate "$FPS" -window_id "$WIN" -i "$DISPLAY" -t "$DUR" \
  -c:v libx264 -preset ultrafast -crf 18 "$OUT/${TAG}.mp4" || true
pkill -KILL -f "$ROM" || true
echo "video: $OUT/${TAG}.mp4"
