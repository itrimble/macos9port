#!/usr/bin/env python3
"""Reject stale external browser and Quake artifacts before integration builds."""
import hashlib
import json
import os
import subprocess
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(repo):
    manifest = json.loads(Path(os.environ['OS999_BROWSER_PROVENANCE']).read_text())
    actual = {str(p.relative_to(repo)): digest(p) for p in (repo / 'browser').rglob('*') if p.is_file()}
    if actual != manifest['sources']:
        raise ValueError('Browser source differs from the browser build provenance')
    browser = Path(os.environ['OS999_BROWSER_BUNDLE']) / 'Contents/MacOS/OS999Browser'
    if digest(browser) != manifest['binary_sha256']:
        raise ValueError('Browser executable differs from the verified build')
    bundle = Path(os.environ['OS999_BROWSER_BUNDLE'])
    actual_bundle = {str(p.relative_to(bundle)): digest(p) for p in bundle.rglob('*')
                     if p.is_file() and not p.is_symlink()}
    if actual_bundle != manifest['bundle_files']:
        raise ValueError('Browser helper/framework files differ from the verified build')
    links = {str(p.relative_to(bundle)): os.readlink(p) for p in bundle.rglob('*') if p.is_symlink()}
    if links != manifest['bundle_links']:
        raise ValueError('Browser framework links differ from the verified build')
    engine = Path(os.environ['OS999_QUAKE_BUILD']) / 'quake3'
    if digest(engine) != os.environ['OS999_QUAKE_SHA256']:
        raise ValueError('Quake engine differs from the verified bridge build')
    header = Path(os.environ['OS999_QUAKE_SOURCE']) / 'code/darwin/os999_bridge.h'
    if digest(header) != digest(repo / 'include/os999_quake_bridge.h'):
        raise ValueError('Quake host and engine protocol headers differ')
    source = os.environ['OS999_QUAKE_SOURCE']
    ref = subprocess.check_output(['git', '-C', source, 'rev-parse', 'HEAD'], text=True).strip()
    if ref != os.environ['OS999_QUAKE_REF']:
        raise ValueError('Quake source checkout differs from pinned commit')
    subprocess.run(['git', '-C', source, 'diff', '--exit-code', 'HEAD', '--'], check=True)
    print('Browser source/binary and Quake binary/protocol provenance match')


if __name__ == '__main__':
    verify(Path.cwd().resolve())
