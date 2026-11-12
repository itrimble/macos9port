#!/bin/sh
# Download the pinned CEF (Chromium Embedded Framework) binary distribution
# and verify its SHA-1 against browser/cef-pin.txt.
#   sh browser/scripts/fetch-cef.sh              # this Mac (macosarm64) -> build/deps/cef
#   sh browser/scripts/fetch-cef.sh linuxarm64   # Raspberry Pi / arm64 Linux -> build/deps/cef-linuxarm64
#   sh browser/scripts/fetch-cef.sh linux64      # x86_64 Linux -> build/deps/cef-linux64
#   sh browser/scripts/fetch-cef.sh macosarm64 --store DIR
#       -> DIR/cef/<version>/ (versioned store used by the integration gate;
#          archives cached in DIR/archives; .archive-sha1 records the check)
set -eu
ROOT_PIN="$(cd "$(dirname "$0")/.." && pwd)/cef-pin.txt"
CEF_VERSION="$(sed -n 's/^CEF_VERSION=//p' "$ROOT_PIN")"
PLATFORM="macosarm64"; STORE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --store) STORE="${2:?--store needs a directory}"; shift 2 ;;
    *) PLATFORM="$1"; shift ;;
  esac
done
case "$PLATFORM" in
  macosarm64) KIND=""; DEST=cef ;;
  linuxarm64) KIND="_minimal"; DEST=cef-linuxarm64 ;;
  linux64) KIND="_minimal"; DEST=cef-linux64 ;;
  *) echo "unknown platform $PLATFORM" >&2; exit 2 ;;
esac
SHA1="$(sed -n "s/^CEF_SHA1_$PLATFORM=//p" "$ROOT_PIN")"
[ -n "$CEF_VERSION" ] && [ -n "$SHA1" ] || { echo "cef-pin.txt lacks CEF_VERSION or CEF_SHA1_$PLATFORM" >&2; exit 2; }
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
if [ -n "$STORE" ]; then
  [ "$PLATFORM" = macosarm64 ] || { echo "--store supports macosarm64 only" >&2; exit 2; }
  DEPS="$STORE/archives"; TARGET="$STORE/cef/$CEF_VERSION"
else
  DEPS="$ROOT/build/deps"; TARGET="$DEPS/$DEST"
fi
NAME="cef_binary_${CEF_VERSION}_${PLATFORM}"
ARCHIVE="$DEPS/$NAME$KIND.tar.bz2"
URL="https://cef-builds.spotifycdn.com/$(printf '%s' "$NAME$KIND" | sed 's/+/%2B/g').tar.bz2"
mkdir -p "$DEPS"
sha1() { (shasum -a 1 "$1" 2>/dev/null || sha1sum "$1") | cut -d' ' -f1; }
if [ -d "$TARGET/include" ] && [ "$(cat "$TARGET/.version" 2>/dev/null)" = "$CEF_VERSION" ]; then
  echo "cef $CEF_VERSION ($PLATFORM) present"; exit 0
fi
if [ ! -f "$ARCHIVE" ] || [ "$(sha1 "$ARCHIVE")" != "$SHA1" ]; then
  curl -q -fL --proto =https --proto-redir =https --tlsv1.2 --connect-timeout 30 --max-time 900 --retry 3 -o "$ARCHIVE.part" "$URL"
  mv "$ARCHIVE.part" "$ARCHIVE"
fi
GOT="$(sha1 "$ARCHIVE")"
[ "$GOT" = "$SHA1" ] || { echo "sha1 mismatch: $GOT" >&2; exit 1; }
EXTRACT="$DEPS/extract.$$"
rm -rf "$EXTRACT"; mkdir -p "$EXTRACT"
tar -xjf "$ARCHIVE" -C "$EXTRACT"
mkdir -p "$(dirname "$TARGET")"
rm -rf "$TARGET.tmp"
mv "$EXTRACT/$NAME$KIND" "$TARGET.tmp"
rmdir "$EXTRACT"
printf '%s' "$CEF_VERSION" > "$TARGET.tmp/.version"
printf '%s' "$SHA1" > "$TARGET.tmp/.archive-sha1"
rm -rf "$TARGET"
mv "$TARGET.tmp" "$TARGET"
echo "cef $CEF_VERSION ready in $TARGET"
