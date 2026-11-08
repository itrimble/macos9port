#!/bin/sh
# Fetch the pinned third-party sources the phone builds compile from.
# They stay in build/deps (ignored by git), like Musashi for the runtime.
#   SDL2      zlib licence   https://github.com/libsdl-org/SDL
#   FreeType  FTL / GPLv2    https://github.com/freetype/freetype
# Run from the repository root: sh mobile/scripts/fetch-deps.sh
set -eu
SDL_TAG=release-2.32.10
FT_TAG=VER-2-15-0
mkdir -p build/deps
fetch() {
    dir=build/deps/$1 url=$2 tag=$3
    if [ -d "$dir/.git" ] && [ "$(git -C "$dir" describe --tags --exact-match 2>/dev/null)" = "$tag" ]; then
        echo "$1 $tag already present"
        return
    fi
    rm -rf "$dir"
    git clone --quiet --depth 1 --branch "$tag" "$url" "$dir"
    echo "$1 $tag fetched"
}
fetch SDL2 https://github.com/libsdl-org/SDL.git "$SDL_TAG"
fetch freetype https://github.com/freetype/freetype.git "$FT_TAG"
