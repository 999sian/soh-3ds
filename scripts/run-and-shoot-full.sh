#!/usr/bin/env bash
# Like the harness run-and-shoot.sh but captures the WHOLE emulator window
# (both 3DS screens) at the given times, for dual-screen verification.
#   ROM=build-3ds-mk/soh_3ds.3dsx ./scripts/run-and-shoot-full.sh <tag> [t1 t2 ...]
set -euo pipefail
TAG="${1:-full}"; shift || true
TIMES=("${@:-120 150 180}")
PRIV=/var/tmp/soh3ds-priv
SHOTS=/var/tmp/shots
APP=${APP:-/tmp/azahar.AppImage}
ROM=${ROM:-build-3ds-mk/soh_3ds.3dsx}
export TMPDIR=/var/tmp DISPLAY=${DISPLAY:-:1}
rm -rf "$PRIV/data/azahar-emu/log"; mkdir -p "$PRIV/data/azahar-emu/log" "$SHOTS"
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
for t in ${TIMES[@]}; do
  while [ "$SECONDS" -lt "$t" ]; do sleep 1; done
  import -window "$WIN" "$SHOTS/${TAG}_full_$(printf %03d "$t").png" || true
  echo "shot t=${t}s"
done
pkill -KILL -f "$ROM" || true
echo "shots: $SHOTS/${TAG}_full_*.png"
