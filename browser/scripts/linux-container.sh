#!/bin/sh
# On the Mac: build + smoke-test the Linux / Raspberry Pi version in Apple's
# `container` (Debian bookworm). Usage: sh browser/scripts/linux-container.sh [linuxarm64|linux64]
# linux64 (x86_64) runs under Rosetta.
set -eu
ARCH="${1:-linuxarm64}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export PATH="/opt/homebrew/bin:$PATH"
sh "$ROOT/browser/scripts/fetch-cef.sh" "$ARCH"
PLAT=$([ "$ARCH" = linux64 ] && echo "--arch amd64" || echo "--arch arm64")
# shellcheck disable=SC2086
container run --rm $PLAT --cpus 2 --memory 4G --volume "$ROOT:/src" debian:bookworm-slim \
  sh /src/browser/scripts/linux-inside.sh "$ARCH"
