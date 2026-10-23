#!/bin/bash
# Reproducible arduino-cli build for the WPM firmware.
# Usage: build.sh [2Wireless|FramLoader] [--profile wpm|wpm_loader|mkr1000] [--verbose] [--tag <t>] [--slow <ms>]
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SKETCH="2Wireless"
PROFILE=""
VERBOSE=""
TAG=""
SLOW=""
while [ $# -gt 0 ]; do
  case "$1" in
    --profile) PROFILE="$2"; shift 2 ;;
    --verbose) VERBOSE="-v"; shift ;;
    --tag) TAG="$2"; shift 2 ;;
    --slow) SLOW="$2"; shift 2 ;;
    2Wireless|FramLoader) SKETCH="$1"; shift ;;
    *) echo "usage: $0 [2Wireless|FramLoader] [--profile wpm|wpm_loader|mkr1000] [--verbose] [--tag <t>] [--slow <ms>]" >&2; exit 2 ;;
  esac
done

if [ -n "$SLOW" ]; then
  [ "$SKETCH" = "FramLoader" ] || { echo "error: --slow applies only to the FramLoader sketch" >&2; exit 2; }
  case "$SLOW" in ''|*[!0-9]*) echo "error: --slow must be a number of milliseconds" >&2; exit 2 ;; esac
  SLOW=$((10#$SLOW))   # never pass a leading zero to the compiler (octal)
fi

if [ -z "$PROFILE" ]; then
  if [ "$SKETCH" = "FramLoader" ]; then PROFILE="wpm_loader"; else PROFILE="wpm"; fi
fi

# Pinned dependencies.
CORE="arduino:samd@1.8.14"
CORE_VER="1.8.14"
WIFI101="WiFi101@0.16.1"
# MIDIUSB and USB_Host_Library_SAMD are vendored (patched) in libraries/; see their WPM-CHANGES.md.

arduino-cli core install "$CORE"
arduino-cli lib install "$WIFI101"

# Board profiles (wpm, wpm_loader) live next to the core's boards.txt.
CORE_DIR="$(arduino-cli config get directories.data)/packages/arduino/hardware/samd/${CORE_VER}"
cp "$HERE/boards.local.txt" "$CORE_DIR/boards.local.txt"

# --tag t: define FW_BUILD_TAG="t" (reported as "build" by /version). The
# property override replaces build.extra_flags, so re-read the profile's own.
# --slow ms (FramLoader only): define LOADER_SLOW_COPY_MS=ms, a per-row delay for power-cut
# benches. Same override mechanism as --tag, so both may be combined.
EXTRA=""
[ -n "$TAG" ] && EXTRA="$EXTRA -DFW_BUILD_TAG=\"$TAG\""
[ -n "$SLOW" ] && EXTRA="$EXTRA -DLOADER_SLOW_COPY_MS=$SLOW"
TAG_ARGS=()
if [ -n "$TAG" ] || [ -n "$SLOW" ]; then
  [ -z "$TAG" ] || case "$TAG" in *[!A-Za-z0-9._-]*) echo "error: --tag must match [A-Za-z0-9._-]+" >&2; exit 2 ;; esac
  BASE_FLAGS=""
  for f in "$HERE/boards.local.txt" "$CORE_DIR/boards.txt"; do
    BASE_FLAGS="$(sed -n "s/^${PROFILE}\.build\.extra_flags=//p" "$f" | head -n1)"
    [ -n "$BASE_FLAGS" ] && break
  done
  [ -n "$BASE_FLAGS" ] || { echo "error: no build.extra_flags for profile $PROFILE" >&2; exit 2; }
  TAG_ARGS=(--build-property "build.extra_flags=$BASE_FLAGS$EXTRA")
fi

# A fresh clone has no loader bin (build/ is untracked) and the 2Wireless build compares against it.
if [ "$SKETCH" = "2Wireless" ] && [ ! -f "$HERE/build/FramLoader/FramLoader.ino.bin" ]; then
  echo "build/FramLoader/FramLoader.ino.bin is missing: building the loader first" >&2
  "$0" FramLoader
fi

OUT="$HERE/build/$SKETCH"
arduino-cli compile --fqbn "arduino:samd:$PROFILE" --libraries "$HERE/libraries" \
  ${TAG_ARGS[@]+"${TAG_ARGS[@]}"} $VERBOSE --output-dir "$OUT" --warnings default "$HERE/$SKETCH"

BIN="$OUT/$SKETCH.ino.bin"
# The wpm and mkr1000 app images are stamped below and its final size/crc printed there; the loader is final as linked.
{ [ "$SKETCH" = "2Wireless" ] && { [ "$PROFILE" = "wpm" ] || [ "$PROFILE" = "mkr1000" ]; }; } || \
python3 -c 'import sys,zlib;d=open(sys.argv[1],"rb").read();print("size %d crc32 %08X"%(len(d),zlib.crc32(d)&0xffffffff))' "$BIN"

if [ "$SKETCH" = "FramLoader" ]; then
  # The loader must fit the 0x4000-byte .sketch_boot slot; embed it in the app.
  python3 "$HERE/tools/blob2c.py" "$BIN" "$HERE/2Wireless/src/wpm_loader_blob.cpp"
  if [ -n "$SLOW" ]; then
    echo "################################################################" >&2
    echo "WARNING: SLOW BENCH LOADER (LOADER_SLOW_COPY_MS=$SLOW)." >&2
    echo "2Wireless/src/wpm_loader_blob.cpp now holds a bench artefact. NEVER commit it:" >&2
    echo "rebuild with 'build.sh FramLoader' (no --slow) before committing." >&2
    echo "################################################################" >&2
  fi
fi

if [ "$SKETCH" = "2Wireless" ] && { [ "$PROFILE" = "wpm" ] || [ "$PROFILE" = "mkr1000" ]; }; then
  # Full image = loader blob (0x2000..0x5FFF) + app (0x6000..). Check both
  # vector tables, stamp the app marker, then emit the app-only OTA image (bin minus first 0x4000).
  python3 - "$BIN" "$OUT/2Wireless.ota.bin" "$HERE/build/FramLoader/FramLoader.ino.bin" <<'PYEOF'
import struct, sys, zlib
d = open(sys.argv[1], "rb").read()
if len(d) <= 0x4000 + 8:
    sys.exit("error: bin too small to contain loader + app")
def vec(off):
    return struct.unpack_from("<II", d, off)
sp, rst = vec(0)
if not (0x20000000 < sp <= 0x20008000) or not (0x2000 <= (rst & ~1) < 0x6000) or not rst & 1:
    sys.exit("error: loader vectors invalid at offset 0: sp=%08X reset=%08X" % (sp, rst))
asp, arst = vec(0x4000)
if not (0x20000000 < asp <= 0x20008000) or not (0x6000 <= (arst & ~1) < 0x40000) or not arst & 1:
    sys.exit("error: app vectors invalid at offset 0x4000: sp=%08X reset=%08X" % (asp, arst))
ldr = open(sys.argv[3], "rb").read()
if d[:0x4000] != ldr + b"\xff" * (0x4000 - len(ldr)):
    sys.exit("error: stale loader blob: run build.sh FramLoader")
if d[0x401C:0x402C] != bytes(16):
    sys.exit("error: app marker slot 0x401C not empty")
ota_len = len(d) - 0x4000
if ota_len % 2 or ota_len > 0x2BD18:
    sys.exit("error: OTA image length %d must be even and <= 0x2BD18" % ota_len)
# Stamp the app marker (vector entries 7..10, reserved on ARMv6-M): magic, link base, length.
d = bytearray(d)
d[0x401C:0x402C] = b"WPMAPP1\0" + struct.pack("<II", 0x6000, ota_len)
d = bytes(d)
open(sys.argv[1], "wb").write(d)
ota = d[0x4000:]
open(sys.argv[2], "wb").write(ota)
print("app marker: base 0x6000 len %d at 0x401C" % ota_len)
print("stamped full image: size %d crc32 %08X" % (len(d), zlib.crc32(d) & 0xffffffff))
print("embedded loader: %d bytes crc32 %08X (blob padded to 0x4000)" % (len(ldr), zlib.crc32(ldr) & 0xffffffff))
print("vectors ok: loader sp=%08X reset=%08X, app sp=%08X reset=%08X" % (sp, rst, asp, arst))
print("ota size %d crc32 %08X -> %s" % (len(ota), zlib.crc32(ota) & 0xffffffff, sys.argv[2]))
PYEOF
  # The .hex and the with_bootloader images are unstamped (no marker) and would hand off to SAM-BA
  # at boot; flash.sh takes the stamped .bin, so drop them.
  rm -f "$OUT/2Wireless.ino.hex" "$OUT/2Wireless.ino.with_bootloader.bin" "$OUT/2Wireless.ino.with_bootloader.hex"
fi
