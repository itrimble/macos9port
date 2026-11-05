#!/bin/sh
# CPU-only dependency; no Apple ROM or implementation is downloaded.
# Retain upstream's MIT-style notices in any distribution that includes it.
set -eu
cd "$(dirname "$0")/.."
revision=313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd
digest=8bed9e28e7e5377256d41aff80a99c9e917a24e7392d9219e1c45c40ad4000e5
archive="build/deps/musashi-$revision.tar.gz"
source="build/deps/Musashi-$revision"
mkdir -p build/deps
if [ ! -f "$archive" ]; then
    download=$(mktemp build/deps/musashi-download.XXXXXX)
    trap 'rm -f "$download"' EXIT HUP INT TERM
    curl --fail --location --proto '=https' --tlsv1.2 --max-time 120 \
        "https://codeload.github.com/kstenerud/Musashi/tar.gz/$revision" -o "$download"
    printf '%s  %s\n' "$digest" "$download" | shasum -a 256 -c -
    mv "$download" "$archive"
    trap - EXIT HUP INT TERM
fi
printf '%s  %s\n' "$digest" "$archive" | shasum -a 256 -c -
if [ ! -d "$source" ]; then
    tar -xzf "$archive" -C build/deps
fi
if [ ! -f "$source/m68kops.c" ] || [ ! -f "$source/m68kops.h" ]; then
    (cd "$source" && "${CC:-cc}" -O2 m68kmake.c -o m68kmake && ./m68kmake)
fi
python3 - "$source/m68kops.c" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
upstream = """static void m68k_op_1010(void)
{
\tm68ki_exception_1010();
}"""
patched = """static void m68k_op_1010(void)
{
\tif (!m68ki_illg_callback(REG_IR)) m68ki_exception_1010();
}"""
if patched not in text:
    if upstream not in text:
        raise SystemExit("Could not locate Musashi 1010 handler for safe patch.")
    path.write_text(text.replace(upstream, patched, 1))
PY
printf 'Musashi source: %s\n' "$source"
