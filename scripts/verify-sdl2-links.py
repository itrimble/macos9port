#!/usr/bin/env python3
"""Verify every tested SDL client uses the pinned private genuine SDL2."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

prefix = Path(os.environ['OS999_SDL2_PREFIX']).resolve()
receipt_dir = Path(os.environ['OS999_RECEIPT_DIR'])
record = json.loads((receipt_dir / 'sdl2-provenance.json').read_text())
for relative, expected in record['files'].items():
    if hashlib.sha256((prefix / relative).read_bytes()).hexdigest() != expected:
        raise SystemExit('Private SDL2 installation changed: ' + relative)
actual_links = {str(p.relative_to(prefix)):str(p.readlink()) for p in prefix.rglob('*') if p.is_symlink()}
if actual_links != record['links']:
    raise SystemExit('Private SDL2 installation symlinks changed')
clients = ['build/ospi', 'build/ospi-runtime', 'build/test_desktop_sound',
           'build/test_audio_engine', 'build/test_runtime_audio', 'build/test_quicktime', 'build/test_quicktime_lifecycle',
           'keyboard/build/test_kbd_sdl', 'build/mobile-gate/os999-mobile-sim',
           str(Path(os.environ['OS999_QUAKE_BUILD']) / 'quake3')]
links = {}
for client in clients:
    output = subprocess.check_output(['/usr/bin/otool', '-L', client], text=True)
    dependencies = [line.strip().split(' (', 1)[0] for line in output.splitlines()[1:]
                    if 'libSDL' in line]
    if len(dependencies) != 1 or not Path(dependencies[0]).resolve().is_relative_to(prefix):
        raise SystemExit('Unpinned SDL linkage: ' + client + ': ' + repr(dependencies))
    links[client] = dependencies[0]
record['client_links'] = links
(receipt_dir / 'sdl2-provenance.json').write_text(json.dumps(record, indent=2) + '\n')
print('All SDL clients link the private genuine SDL2 build')
