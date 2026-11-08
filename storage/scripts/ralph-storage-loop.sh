#!/bin/sh
# Ralph gate for the OS999 storage stack (checklist L02-L04).
#
# One pass = warning-free release build, all headless tests, a sanitizer
# build of the tests plus mutation fuzzing, real-fixture checks when
# OS999_STORAGE_FIXTURES is set, the 500-line file rule, and the feature
# spec in storage/FEATURES.md. Evidence goes to build/ralph-storage/<UTC>.
# Exit 0 only when every step passes AND no required feature is open.
# The loop: run this, fix the first failure or the next open feature,
# rerun. A retry is never a fix.
#
#   sh storage/scripts/ralph-storage-loop.sh [--passes N] [--fuzz ITERS]
set -u
cd "$(dirname "$0")/../.." || exit 2
PASSES=1
FUZZ=${OS999_STORAGE_FUZZ_ITERS:-5000}
while [ $# -gt 0 ]; do
    case "$1" in
    --passes) PASSES=$2; shift 2 ;;
    --fuzz) FUZZ=$2; shift 2 ;;
    *) echo "usage: $0 [--passes N] [--fuzz ITERS]" >&2; exit 2 ;;
    esac
done
STAMP=$(date -u +%Y%m%dT%H%M%SZ)
OUT=build/ralph-storage/$STAMP
mkdir -p "$OUT"
SUMMARY=$OUT/summary.txt
fail=0

step() { # name, command...
    name=$1; shift
    log=$OUT/$pass-$name.log
    if "$@" >"$log" 2>&1; then
        echo "pass $pass  ok    $name" | tee -a "$SUMMARY"
    else
        echo "pass $pass  FAIL  $name  (see $log)" | tee -a "$SUMMARY"
        tail -20 "$log"
        fail=1
    fi
}

lines_ok() {
    bad=0
    for f in storage/src/*.c storage/src/*.h storage/include/*.h storage/tests/*.c storage/tests/*.h storage/tools/*.c; do
        n=$(wc -l <"$f")
        if [ "$n" -ge 500 ]; then echo "$f has $n lines"; bad=1; fi
    done
    return $bad
}

{
    echo "host: $(uname -a)"
    echo "cc: $(${CC:-cc} --version 2>&1 | head -1)"
    echo "base: $(git rev-parse HEAD 2>/dev/null)"
    echo "fuzz iterations per format: $FUZZ"
    echo "fixtures: ${OS999_STORAGE_FIXTURES:-unset}"
} >"$OUT/environment.txt"
(find storage -type f \( -name '*.c' -o -name '*.h' -o -name '*.mk' -o -name '*.md' -o -name '*.sh' \) | sort |
    while read -r f; do
        if command -v sha256sum >/dev/null 2>&1; then sha256sum "$f"; else shasum -a 256 "$f"; fi
    done) >"$OUT/source-manifest.sha256"

pass=1
while [ "$pass" -le "$PASSES" ]; do
    rm -rf build/storage build/storage-asan
    step build make -f storage/storage.mk STORAGE_CFLAGS="-std=c11 -Wall -Wextra -Werror -Wno-format-truncation -O2 -D_DEFAULT_SOURCE" build/storage/libos999storage.a build/os999-unpack
    step tests make -f storage/storage.mk test-storage
    step sanitizers env OS999_STORAGE_FUZZ_ITERS="$FUZZ" make -f storage/storage.mk storage-asan
    step file-size lines_ok
    pass=$((pass + 1))
done

open=$(sed -n '/^## Required/,/^## /p' storage/FEATURES.md | grep -c '^- \[ \]')
done_n=$(sed -n '/^## Required/,/^## /p' storage/FEATURES.md | grep -c '^- \[x\]')
echo "features: $done_n done, $open open" | tee -a "$SUMMARY"
sed -n '/^## Required/,/^## /p' storage/FEATURES.md | grep '^- \[ \]' | tee -a "$SUMMARY"
echo "evidence: $OUT" | tee -a "$SUMMARY"
if [ "$fail" -ne 0 ]; then echo "GATE: FAIL"; exit 1; fi
if [ "$open" -ne 0 ]; then echo "GATE: OPEN ($open required features left)"; exit 3; fi
echo "GATE: PASS"
exit 0
