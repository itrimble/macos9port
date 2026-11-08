#!/bin/sh
# A receipt records automated build validation, never gameplay acceptance.
set -eu
repo=$1
: "${OS999_RECEIPT_DIR:?Receipt directory required}"
: "${OS999_SOURCE_TREE:?Source tree required}"
: "${OS999_BUILD_NUMBER:?Build number required}"
export BUILDFLEET_LOCAL=1
cd "$repo"
. scripts/package-inputs.sh
python3 scripts/verify-package-inputs.py
python3 tests/test_complete_package.py
python3 tests/test_quake_package.py
# Make does not track dependency flags: force every existing SDL-linked target.
make -j2 -W src/main.c -W src/runtime_audio.c -W src/desktop_sound.c \
    -W src/browser_overlay_mac.m test all test-wallpapers test-desktop-sound
make test-tailnet
python3 tests/test_network_tls.py
make -C keyboard -f keyboard.mk -W src/kbd_sdl.c test test-sdl
make -f mobile/mobile.mk OUT=build/mobile-gate -W mobile/sdl/mobile_main.c test sim
python3 scripts/verify-sdl2-links.py
app="$OS999_RECEIPT_DIR/OS999 Build $OS999_BUILD_NUMBER.app"
# Refuse replacement, including an earlier or live bundle.
test ! -e "$app"
make app APP_BUNDLE="$app" QUAKE_SRC="$OS999_QUAKE_SOURCE" QUAKE_BUILD="$OS999_QUAKE_BUILD"
cp "$OS999_SDL2_PREFIX/../SDL2-2.32.10/LICENSE.txt" "$app/Contents/Resources/SDL2-LICENSE.txt"
# Complete packaging resolves shared libraries and verifies the nested signature.
python3 scripts/complete-package.py "$app" "$OS999_BROWSER_BUNDLE"
python3 scripts/package-smoke.py "$app" --output "$OS999_RECEIPT_DIR"
python3 - "$app" <<'PY'
import hashlib,json,os,pathlib,sys
app=pathlib.Path(sys.argv[1])
for relative in ('Contents/MacOS/ospi','Contents/MacOS/ospi-runtime',
                 'Contents/Frameworks/OS999Browser.app', 'Contents/Resources/Quake3/quake3'):
    if not (app/relative).exists():
        raise SystemExit('Required packaged component missing: '+relative)
files={str(p.relative_to(app)):hashlib.sha256(p.read_bytes()).hexdigest()
       for p in app.rglob('*') if p.is_file() and not p.is_symlink()}
receipt={'tree':os.environ['OS999_SOURCE_TREE'],'result':'pass','artifact':str(app),
         'number':int(os.environ['OS999_BUILD_NUMBER']),
         'title':os.environ.get('OS999_BUILD_TITLE','OS999 integration'),
         'files':files,'scope':'Automated build/tests, signature and relocated seed; GUI acceptance pending'}
receipt['relocation']=json.loads((pathlib.Path(os.environ['OS999_RECEIPT_DIR'])/'relocation.json').read_text())
receipt['inputs']={'quake_profile':json.loads((pathlib.Path(os.environ['OS999_QUAKE_BUILD'])/'arena111-package.json').read_text()),
                   'sdl2':json.loads((pathlib.Path(os.environ['OS999_RECEIPT_DIR'])/'sdl2-provenance.json').read_text()),'browser_manifest_sha256':hashlib.sha256(pathlib.Path(os.environ['OS999_BROWSER_PROVENANCE']).read_bytes()).hexdigest(),
                   'quake_commit':os.environ['OS999_QUAKE_REF'], 'quake_prepackage_sha256':os.environ['OS999_QUAKE_SHA256'],
                   'quake_protocol_sha256':hashlib.sha256(pathlib.Path('include/os999_quake_bridge.h').read_bytes()).hexdigest()}
path=pathlib.Path(os.environ['OS999_RECEIPT_DIR'])/'receipt.json'
tmp=path.with_suffix('.tmp');tmp.write_text(json.dumps(receipt,indent=2)+'\n');tmp.replace(path)
PY
