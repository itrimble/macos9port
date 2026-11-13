#!/bin/sh
# Fetch CEF (if needed), then configure and build OS999 Browser.app.
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
sh "$ROOT/browser/scripts/fetch-cef.sh"
cmake -S "$ROOT/browser" -B "$ROOT/build/browser" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPROJECT_ARCH=arm64
cmake --build "$ROOT/build/browser" --target OS999Browser version_policy_test decode_diag_test
"$(find "$ROOT/build/browser" -name version_policy_test -type f -perm -u+x | head -1)"  # B06 policy tests
"$(find "$ROOT/build/browser" -name decode_diag_test -type f -perm -u+x | head -1)"  # decoder diagnostics tests
APP="$(find "$ROOT/build/browser" -name 'OS999Browser.app' -maxdepth 3 -type d | head -1)"
# Sign with a stable identity when one exists. macOS keys privacy approvals
# (Local Network access) to the signer; ad-hoc signatures change on every
# build, so each rebuild would silently lose network access until re-approved.
IDENTITY="${OS999_SIGN_IDENTITY:-$(security find-identity -v -p codesigning 2>/dev/null | sed -n 's/.*"\(Apple Development:[^"]*\)".*/\1/p' | head -1)}"
python3 "$ROOT/browser/scripts/sign.py" "$APP" --identity "${IDENTITY:--}"
echo "signed: ${IDENTITY:-ad-hoc}"
echo "built: $APP"
