#!/usr/bin/env bash
# Run a .3dsx in Azahar for a long window and keep the title's output.
#
#   ./scripts/run-and-capture.sh build-3ds/soh_3ds.3dsx 900 [out.log]
#
# run-in-azahar.sh alone is not enough for runs of several minutes: Azahar keeps
# only azahar_log.txt plus one .old, and a long session can leave both without
# the boot output. This snapshots the live log while the run is in progress and
# accumulates the title's lines, so a slow boot stays observable.
set -uo pipefail

ROM="${1:?usage: run-and-capture.sh <file.3dsx> [seconds] [out.log]}"
SECS="${2:-900}"
OUT="${3:-/tmp/soh3ds-capture.log}"
LIVE="$HOME/.local/share/azahar-emu/log/azahar_log.txt"
HERE="$(cd "$(dirname "$0")" && pwd)"

: > "$OUT"
"$HERE/run-in-azahar.sh" "$ROM" "$SECS" >/dev/null 2>&1 &
RUNNER=$!

# Poll rather than tail -F: the file is recreated on launch and can be rotated
# mid-run, and a snapshot every few seconds costs nothing next to the emulator.
#
# Append only the bytes each file has grown by. Re-catting the whole log every
# 5s made .raw grow quadratically - a 4-minute run against a busy log produced
# a 6 GB intermediate and filled a 16 GB /tmp, which silently breaks the very
# run being captured.
declare -A SEEN_BYTES
snapshot() {
    local f size prev
    for f in "$LIVE" "${LIVE%.txt}.old.txt"; do
        [ -f "$f" ] || continue
        size=$(stat -c %s "$f" 2>/dev/null) || continue
        prev=${SEEN_BYTES[$f]:-0}
        # A shrink means Azahar recreated or rotated the file, so the offset no
        # longer refers to anything; restart it rather than skip the new head.
        [ "$size" -lt "$prev" ] && prev=0
        if [ "$size" -gt "$prev" ]; then
            tail -c "+$((prev + 1))" "$f" >> "$OUT.raw" 2>/dev/null
        fi
        SEEN_BYTES[$f]=$size
    done
}

while kill -0 "$RUNNER" 2>/dev/null; do
    snapshot
    sleep 5
done
wait "$RUNNER" 2>/dev/null

snapshot

# Keep first-seen order, drop the duplicates repeated snapshots produce.
awk '!seen[$0]++' "$OUT.raw" > "$OUT" 2>/dev/null
rm -f "$OUT.raw"
echo "captured $(wc -l < "$OUT") log lines -> $OUT"
