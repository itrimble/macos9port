#!/usr/bin/env python3
"""Copy authenticated Quake profile modules without changing signed bytes."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def profile(root):
    record = json.loads((root / 'arena111-package.json').read_text())
    names = record['modules']
    if record['profile'] != 'arena-1.11' or len(names) != 3:
        raise ValueError('Invalid Arena package profile')
    arches = {re.fullmatch(r'(?:qagame|cgame|ui)(arm64|x86_64)\.so', n).group(1)
              for n in names if re.fullmatch(r'(?:qagame|cgame|ui)(arm64|x86_64)\.so', n)}
    if len(arches) != 1 or set(names) != {n + next(iter(arches)) + '.so' for n in ('qagame', 'cgame', 'ui')}:
        raise ValueError('Invalid Arena native module inventory')
    if record['overlay']['file'] != 'arena111-overlay.pk3':
        raise ValueError('Invalid Arena overlay path')
    return record


def verified_modules(root):
    manifest = root / 'arena111-package.json'
    if not manifest.exists():
        return {}
    record = profile(root)
    result = {}
    for folder in ('baseq3', 'demoq3'):
        for name, expected in record['modules'].items():
            path = root / folder / name
            if path.is_symlink() or digest(path) != expected:
                raise ValueError('Arena module differs from engine manifest: ' + str(path))
            subprocess.run(['/usr/bin/codesign', '--verify', '--strict', str(path)], check=True)
            result[path] = expected
    overlay = root / 'demoq3/zz-arena111.pk3'
    if overlay.is_symlink() or digest(overlay) != record['overlay']['sha256']:
        raise ValueError('Arena overlay differs from engine manifest')
    return result


def verified_build(build):
    record = profile(build)
    for name, expected in record['modules'].items():
        path = build / 'baseq3' / name
        if path.is_symlink() or digest(path) != expected:
            raise ValueError('Unverified Arena build module: ' + name)
        subprocess.run(['/usr/bin/codesign', '--verify', '--strict', str(path)], check=True)
    overlay = build / record['overlay']['file']
    if overlay.is_symlink() or digest(overlay) != record['overlay']['sha256']:
        raise ValueError('Arena overlay differs from engine manifest')
    return record


def copy_profile(build, destination):
    record = verified_build(build)
    (destination / 'demoq3').mkdir(exist_ok=True)
    for name, expected in record['modules'].items():
        source = build / 'baseq3' / name
        if source.is_symlink() or digest(source) != expected:
            raise ValueError('Unverified Arena build module: ' + name)
        shutil.copy2(source, destination / 'demoq3' / name)
    shutil.copy2(build / record['overlay']['file'], destination / 'demoq3/zz-arena111.pk3')
    shutil.copy2(build / 'arena111-package.json', destination / 'arena111-package.json')
    verified_modules(destination)


if __name__ == '__main__':
    copy_profile(Path(sys.argv[1]), Path(sys.argv[2]))
