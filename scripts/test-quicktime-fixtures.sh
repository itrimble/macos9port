#!/bin/sh
set -eu
root=$(mktemp -d "${TMPDIR:-/tmp}/os999-qt-fixtures.XXXXXX")
# Entirely generated patterns/tones. No proprietary movie or codec samples.
ffmpeg -nostdin -hide_banner -loglevel error -f lavfi -i 'testsrc2=size=64x48:rate=12' -f lavfi -i 'sine=frequency=440:sample_rate=48000' -t 2 -c:v libx264 -threads 2 -pix_fmt yuv420p -c:a aac "$root/h264-aac.mp4"
ffmpeg -nostdin -hide_banner -loglevel error -f lavfi -i 'testsrc2=size=64x48:rate=12' -t 2 -c:v qtrle -threads 2 -pix_fmt argb "$root/animation.mov"
ffmpeg -nostdin -hide_banner -loglevel error -f lavfi -i 'testsrc2=size=64x48:rate=12' -t 2 -c:v qtrle -threads 2 -pix_fmt gray "$root/animation8.mov"
ffmpeg -nostdin -hide_banner -loglevel error -f lavfi -i 'testsrc2=size=64x48:rate=12' -t 2 -c:v rpza -threads 2 -pix_fmt rgb555le -f mov "$root/video16.qt"
./build/test_quicktime "$root/h264-aac.mp4" "$root/animation.mov" "$root/animation8.mov" "$root/video16.qt"
./build/test_quicktime_lifecycle "$root/h264-aac.mp4"
printf 'Generated fixture evidence: %s\n' "$root"
