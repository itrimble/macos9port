#!/bin/sh
# Download the pinned CEF (Chromium Embedded Framework) binary distribution
# and verify its SHA-1 from the CEF build index.
#   sh browser/scripts/fetch-cef.sh              # this Mac (macosarm64) -> build/deps/cef
#   sh browser/scripts/fetch-cef.sh linuxarm64   # Raspberry Pi / arm64 Linux -> build/deps/cef-linuxarm64
#   sh browser/scripts/fetch-cef.sh linux64      # x86_64 Linux -> build/deps/cef-linux64
set -eu
ROOT_PIN="$(cd "$(dirname "$0")/.." && pwd)/cef-pin.txt"
CEF_VERSION="$(sed -n 's/^CEF_VERSION=//p' "$ROOT_PIN")"
PLATFORM="${1:-macosarm64}"
case "$PLATFORM" in
  macosarm64) KIND=""; SHA1="6989ef3226be063ae164d1d9f0764a2f203d4cb1"; DEST=cef ;;
  linuxarm64) KIND="_minimal"; SHA1="1e66c55ca1a7e4de7c37b3c056435999c7420ebf"; DEST=cef-linuxarm64 ;;
  linux64) KIND="_minimal"; SHA1="cacfbb7d7261fea682fb67c108de7e4347519ff7"; DEST=cef-linux64 ;;
  *) echo "unknown platform $PLATFORM" >&2; exit 2 ;;
esac
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEPS="$ROOT/build/deps"
NAME="cef_binary_${CEF_VERSION}_${PLATFORM}"
ARCHIVE="$DEPS/$NAME$KIND.tar.bz2"
URL="https://cef-builds.spotifycdn.com/$(printf '%s' "$NAME$KIND" | sed 's/+/%2B/g').tar.bz2"
mkdir -p "$DEPS"
sha1() { (shasum -a 1 "$1" 2>/dev/null || sha1sum "$1") | cut -d' ' -f1; }
if [ -d "$DEPS/$DEST/include" ] && [ "$(cat "$DEPS/$DEST/.version" 2>/dev/null)" = "$CEF_VERSION" ]; then
  echo "cef $CEF_VERSION ($PLATFORM) present"; exit 0
fi
if [ ! -f "$ARCHIVE" ] || [ "$(sha1 "$ARCHIVE")" != "$SHA1" ]; then
  curl -q -fL --proto =https --proto-redir =https --tlsv1.2 --connect-timeout 30 --max-time 900 --retry 3 -o "$ARCHIVE.part" "$URL"
  mv "$ARCHIVE.part" "$ARCHIVE"
fi
GOT="$(sha1 "$ARCHIVE")"
[ "$GOT" = "$SHA1" ] || { echo "sha1 mismatch: $GOT" >&2; exit 1; }
rm -rf "$DEPS/$NAME$KIND" "$DEPS/$DEST.tmp"
tar -xjf "$ARCHIVE" -C "$DEPS"
mv "$DEPS/$NAME$KIND" "$DEPS/$DEST.tmp"
rm -rf "$DEPS/$DEST"
mv "$DEPS/$DEST.tmp" "$DEPS/$DEST"
printf '%s' "$CEF_VERSION" > "$DEPS/$DEST/.version"
echo "cef $CEF_VERSION ready in $DEPS/$DEST"
