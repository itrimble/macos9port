#!/bin/zsh
# Build-only conversion of the original delivered 4:3 masters. No source assets are modified.
# Usage: zsh scripts/prepare-wallpapers.sh SOURCE_ASSETS OUTPUT_DIRECTORY
set -euo pipefail
[[ $# == 2 ]] || { print -u2 'usage: prepare-wallpapers.sh ASSETS OUTPUT_DIRECTORY'; exit 2; }
source_dir=${1:A}/wallpapers/4x3
output_dir=${2:A}
[[ -d "$source_dir" ]] || { print -u2 "missing source: $source_dir"; exit 1; }
[[ "$output_dir" != "$source_dir" ]] || exit 2
mkdir -p "$output_dir"
convert() {
    local id=$1 source=$2 target="$output_dir/$1.bmp"
    [[ -f "$source_dir/$source" ]] || { print -u2 "missing $source"; return 1; }
    if [[ ! -f "$target" || "$source_dir/$source" -nt "$target" ]]; then
        local temporary
        temporary=$(mktemp -d "$output_dir/.wallpaper-$id.XXXXXX")
        if /usr/bin/sips -s format bmp "$source_dir/$source" --out "$temporary/image.bmp" >/dev/null; then
            mv "$temporary/image.bmp" "$target"
            rmdir "$temporary"
        else
            rm -f "$temporary/image.bmp"
            rmdir "$temporary"
            return 1
        fi
    fi
}
convert 01 '01-1984-halftone-grey.png'
convert 02 '02-1986-basket-weave.png'
convert 03 '03-1988-tidal-tile.png'
convert 04 '04-1991-periwinkle-speckle.png'
convert 05 '05-1994-platinum-ripple.png'
convert 06 '06-1997-lavender-bubbles.png'
convert 07 '07-1998-translucent-teal.jpg'
convert 08 '08-1999-five-flavors.jpg'
convert 09 '09-2000-graphite.jpg'
convert 10 '10-2001-aqua-swoosh.jpg'
convert 11 '11-2002-aqua-pinstripe.jpg'
convert 12 '12-2003-brushed-metal.jpg'
convert 13 '13-2005-aurora-blue.jpg'
convert 14 '14-2007-nebula-violet.jpg'
convert 15 '15-2009-snow-aurora.jpg'
convert 16 '16-2011-spiral-galaxy.jpg'
convert 17 '17-2013-big-wave.jpg'
convert 18 '18-2014-granite-peaks.jpg'
convert 19 '19-2015-night-cliff.jpg'
convert 20 '20-2016-blue-ridges.jpg'
convert 21 '21-2017-alpine-lake.jpg'
convert 22 '22-2018-dunes-day.jpg'
convert 23 '23-2018-dunes-night.jpg'
convert 24 '24-2019-island-cliff.jpg'
convert 25 '25-2020-coastal-gradient.jpg'
convert 26 '26-2021-silk-folds.jpg'
convert 27 '27-2022-bloom.jpg'
convert 28 '28-2023-vineyard-hills.jpg'
convert 29 '29-2024-glow-waves.jpg'
convert 30 '30-2025-liquid-glass.jpg'
convert 31 '31-2001-cheetah.jpg'
convert 32 '32-2001-puma.jpg'
convert 33 '33-2002-jaguar.jpg'
convert 34 '34-2003-panther.jpg'
convert 35 '35-2005-tiger.jpg'
convert 36 '36-2007-leopard.jpg'
convert 37 '37-2009-snow-leopard.jpg'
convert 38 '38-2011-lion.jpg'
convert 39 '39-2012-mountain-lion.jpg'
convert 40 '40-1999-aqua-bokeh.jpg'
convert 41 '41-1999-teal-drift.jpg'
convert 42 '42-1999-teal-bloom.jpg'
convert 43 '43-2000-glass-shelf.jpg'
convert 44 '44-2000-daisy-pop.jpg'
convert 45 '45-2000-amber-cosmos.jpg'
convert 46 '46-1999-violet-drift.jpg'
convert 47 '47-1999-violet-colony.jpg'
convert 48 '48-1999-lime-spiral.jpg'
convert 49 '49-1999-lime-drift.jpg'
convert 50 '50-1999-pi-desktop.jpg'
convert 51 '51-2001-silk-curves.jpg'
convert 52 '52-2000-terracotta-shutters.jpg'
convert 53 '53-1999-berry-cells.jpg'
convert 54 '54-1999-berry-drift.jpg'
convert 55 '55-1999-tangerine-coral.jpg'
convert 56 '56-1999-tangerine-drift.jpg'
convert 57 '57-2000-tangerine-stars.jpg'
convert 58 '58-1999-lime-saucers.jpg'
convert 59 '59-2000-golden-lenses.jpg'
