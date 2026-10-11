#!/usr/bin/env bash
# Copy the EXTRACTED disc (default.xbe, NFSUNDER/, ...; never the ISO) to the
# phone's app folder over adb. Android 11+ hides Android/data from file
# managers, so adb (or a USB cable in file-transfer mode) is the way in.
#
#   android/tools/push_game.sh /path/to/extracted/disc
#   PACKAGE=com.nfsu2x.nfsu2 (default)
set -euo pipefail
SRC="${1:?usage: push_game.sh <extracted disc dir>}"
PKG="${PACKAGE:-com.nfsu2x.nfsu2}"
DEST="/sdcard/Android/data/$PKG/files/game"
[ -f "$SRC/default.xbe" ] || { echo "no default.xbe in $SRC" >&2; exit 1; }
adb shell mkdir -p "$DEST"
adb push "$SRC/." "$DEST/"
echo "pushed to $DEST"
