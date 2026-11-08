#!/bin/sh
# Private genuine SDL2: SDL2-compat rejects synthetic SDL_TEXTINPUT events.
set -eu
version=2.32.10
digest=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
cache=$(pwd)/build/deps
mkdir -p "$cache"
root=$(mktemp -d /tmp/os999-sdl2.XXXXXX)
archive="$cache/SDL2-$version.tar.gz"
source="$root/SDL2-$version"
prefix="$root/sdl2-$version-install"
mkdir -p "$root"
if [ ! -f "$archive" ]; then
    curl --fail --location --proto '=https' --tlsv1.2 --max-time 120 \
        "https://github.com/libsdl-org/SDL/releases/download/release-$version/SDL2-$version.tar.gz" \
        -o "$archive.download"
    printf '%s  %s\n' "$digest" "$archive.download" | shasum -a 256 -c -
    mv "$archive.download" "$archive"
fi
printf '%s  %s\n' "$digest" "$archive" | shasum -a 256 -c -
# Each run gets fresh source/build/install trees; retain them as build evidence.
tar -xzf "$archive" -C "$root"
cmake -S "$source" -B "$root/sdl2-$version-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_NAME_DIR="$prefix/lib" \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL_TESTS=OFF \
    -DCMAKE_SHARED_LINKER_FLAGS=-Wl,-headerpad_max_install_names
cmake --build "$root/sdl2-$version-build" -j2
cmake --install "$root/sdl2-$version-build"
python3 - "$prefix" "$archive" "$OS999_RECEIPT_DIR/sdl2-provenance.json" <<'PY'
import hashlib,json,pathlib,sys
prefix,archive,output=map(pathlib.Path,sys.argv[1:])
record={'version':'2.32.10','archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),
        'reason':'Genuine SDL2 preserves synthetic text input; SDL2-compat crashes converting it',
        'prefix':str(prefix),'links':{str(p.relative_to(prefix)):str(p.readlink())
                                    for p in prefix.rglob('*') if p.is_symlink()},
        'files':{str(p.relative_to(prefix)):hashlib.sha256(p.read_bytes()).hexdigest()
                                    for p in prefix.rglob('*') if p.is_file() and not p.is_symlink()}}
output.write_text(json.dumps(record,indent=2)+'\n')
PY
