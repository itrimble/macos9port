#!/bin/sh
# Sourced by integration-gate.sh; all generated artifacts remain outside Git.
: "${OS999_CEF_ROOT:?Pinned CEF distribution required}"
: "${OS999_QUAKE_REPOSITORY:?External engine repository required}"
: "${OS999_QUAKE_REF:?Immutable external engine commit required}"
mkdir -p "$OS999_RECEIPT_DIR"
sh scripts/prepare-sdl2.sh
OS999_SDL2_PREFIX=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["prefix"])' "$OS999_RECEIPT_DIR/sdl2-provenance.json")
export OS999_SDL2_PREFIX
export PKG_CONFIG_PATH="$OS999_SDL2_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export PATH="$OS999_SDL2_PREFIX/bin:$PATH"
test "$(pkg-config --modversion sdl2)" = 2.32.10
test "$(pkg-config --variable=prefix sdl2)" = "$OS999_SDL2_PREFIX"
python3 - <<'PY'
import os,pathlib,re,subprocess
ref=os.environ['OS999_QUAKE_REF']
if not re.fullmatch('[0-9a-f]{40}', ref):
    raise SystemExit('Quake reference must be a full immutable commit')
resolved=subprocess.check_output(['git','-C',os.environ['OS999_QUAKE_REPOSITORY'],
                                 'rev-parse',ref+'^{commit}'],text=True).strip()
if resolved != ref:
    raise SystemExit('Quake commit did not resolve exactly')
pin=dict(line.split('=',1) for line in pathlib.Path('browser/cef-pin.txt').read_text().splitlines()
         if line and not line.startswith('#'))
if (pathlib.Path(os.environ['OS999_CEF_ROOT'])/'.version').read_text().strip() != pin['CEF_VERSION']:
    raise SystemExit('CEF distribution does not match candidate pin')
PY
cmake -S browser -B build/browser -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DPROJECT_ARCH=arm64 -DOS999_ENABLE_TEST_HOOKS=OFF -DCEF_ROOT="$OS999_CEF_ROOT"
cmake --build build/browser --target OS999Browser version_policy_test launch_policy_test -j2
./build/browser/version_policy_test
./build/browser/launch_policy_test
export OS999_BROWSER_BUNDLE="$repo/build/browser/Release/OS999Browser.app"
python3 browser/scripts/sign.py "$OS999_BROWSER_BUNDLE"
export OS999_BROWSER_PROVENANCE="$OS999_RECEIPT_DIR/browser-provenance.json"
python3 - <<'PY'
import hashlib,json,os,pathlib
root=pathlib.Path.cwd()
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
bundle=pathlib.Path(os.environ['OS999_BROWSER_BUNDLE'])
record={'sources':{str(p.relative_to(root)):digest(p) for p in (root/'browser').rglob('*') if p.is_file()},
        'binary_sha256':digest(bundle/'Contents/MacOS/OS999Browser'),
        'bundle_files':{str(p.relative_to(bundle)):digest(p) for p in bundle.rglob('*')
                        if p.is_file() and not p.is_symlink()},
        'bundle_links':{str(p.relative_to(bundle)):os.readlink(p) for p in bundle.rglob('*') if p.is_symlink()},
        'cef_version':(pathlib.Path(os.environ['OS999_CEF_ROOT'])/'.version').read_text().strip()}
pathlib.Path(os.environ['OS999_BROWSER_PROVENANCE']).write_text(json.dumps(record,indent=2)+'\n')
PY
export OS999_QUAKE_SOURCE="$OS999_RECEIPT_DIR/quake-source"
git -C "$OS999_QUAKE_REPOSITORY" worktree add --detach "$OS999_QUAKE_SOURCE" "$OS999_QUAKE_REF"
cmp include/os999_quake_bridge.h "$OS999_QUAKE_SOURCE/code/darwin/os999_bridge.h"
make -C "$OS999_QUAKE_SOURCE/code/darwin" -j2 all test-os999-bridge
export OS999_QUAKE_BUILD="$OS999_QUAKE_SOURCE/build/darwin"
OS999_QUAKE_SHA256=$(shasum -a 256 "$OS999_QUAKE_BUILD/quake3" | cut -d ' ' -f1)
export OS999_QUAKE_SHA256
