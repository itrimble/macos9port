#!/bin/sh
# Test dependencies for OS999 on hosts without root or a package mirror
# (for example the Linux VM behind Claude's device bridge).
#
#   sh storage/scripts/setup-test-deps.sh [--fixtures DIR]
#   . build/deps/env.sh && make test
#
# Linux: builds pinned SDL2 and bzip2 into build/deps/prefix (git-ignored)
# and writes build/deps/env.sh. macOS: prints the Homebrew command and
# installs nothing. --fixtures DIR links real-file test inputs (kept
# outside Git) to build/fixtures for OS999_STORAGE_FIXTURES.
set -eu
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
DEPS=$ROOT/build/deps
PREFIX=$DEPS/prefix
FIXTURES=
while [ $# -gt 0 ]; do
    case "$1" in
    --fixtures) FIXTURES=$2; shift 2 ;;
    *) echo "usage: $0 [--fixtures DIR]" >&2; exit 2 ;;
    esac
done

if [ "$(uname -s)" = Darwin ]; then
    echo "macOS: install the test dependencies with Homebrew:"
    echo "  brew install sdl2 freetype unar pkg-config"
    echo "zlib and bzip2 ship with the macOS SDK. Then: make test"
    exit 0
fi

SDL_VER=2.32.10
SDL_SHA=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
BZ_TAG=bzip2-1.0.8
BZ_COMMIT=6a8690fc8d26c815e798c588f796eabe9d684cf0
JOBS=$(nproc 2>/dev/null || echo 2)
mkdir -p "$DEPS" "$PREFIX/include" "$PREFIX/lib"

sha() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -c1-64; else shasum -a 256 "$1" | cut -c1-64; fi; }

if [ ! -f "$PREFIX/lib/pkgconfig/sdl2.pc" ]; then
    tgz=$DEPS/SDL2-$SDL_VER.tar.gz
    [ -f "$tgz" ] || curl --fail --location --proto '=https' --max-time 300 \
        "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.tar.gz" -o "$tgz"
    [ "$(sha "$tgz")" = "$SDL_SHA" ] || { echo "SDL2 archive digest mismatch" >&2; rm -f "$tgz"; exit 1; }
    rm -rf "$DEPS/SDL2-$SDL_VER"
    tar -xzf "$tgz" -C "$DEPS"
    (cd "$DEPS/SDL2-$SDL_VER" && ./configure --prefix="$PREFIX" --disable-static >"$DEPS/sdl2-configure.log" 2>&1 &&
        make -j"$JOBS" >"$DEPS/sdl2-build.log" 2>&1 && make install >>"$DEPS/sdl2-build.log" 2>&1)
    echo "SDL2 $SDL_VER installed in $PREFIX"
fi

if [ ! -f "$PREFIX/lib/libbz2.a" ]; then
    rm -rf "$DEPS/bzip2"
    git clone --quiet --depth 1 --branch "$BZ_TAG" https://github.com/libarchive/bzip2 "$DEPS/bzip2"
    [ "$(git -C "$DEPS/bzip2" rev-parse HEAD)" = "$BZ_COMMIT" ] || { echo "bzip2 tag commit mismatch" >&2; exit 1; }
    make -C "$DEPS/bzip2" -j"$JOBS" libbz2.a >"$DEPS/bzip2-build.log" 2>&1
    cp "$DEPS/bzip2/bzlib.h" "$PREFIX/include/"
    cp "$DEPS/bzip2/libbz2.a" "$PREFIX/lib/"
    echo "bzip2 ($BZ_COMMIT) installed in $PREFIX"
fi

if [ -n "$FIXTURES" ]; then
    rm -f "$ROOT/build/fixtures"
    ln -s "$(cd "$FIXTURES" && pwd)" "$ROOT/build/fixtures"
fi

cat >"$DEPS/env.sh" <<EOF
# Source before make: . build/deps/env.sh
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig\${PKG_CONFIG_PATH:+:\$PKG_CONFIG_PATH}"
export CPATH="$PREFIX/include\${CPATH:+:\$CPATH}"
export LIBRARY_PATH="$PREFIX/lib\${LIBRARY_PATH:+:\$LIBRARY_PATH}"
export LD_LIBRARY_PATH="$PREFIX/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
[ -d "$ROOT/build/fixtures" ] && export OS999_STORAGE_FIXTURES="$ROOT/build/fixtures"
EOF
command -v unar >/dev/null || echo "note: unar is not installed; the unar fallback checks will report SKIP"
echo "wrote $DEPS/env.sh"
