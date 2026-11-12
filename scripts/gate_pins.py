#!/usr/bin/env python3
"""Integration preflight: resolve every external input from the candidate tree.

Pins live in the candidate source (browser/cef-pin.txt, config/quake-pin.txt,
and the digests inside scripts/prepare-*.sh). Bytes live outside every worktree
in a versioned store. The launch-agent environment may supply only locations
(OS999_QUAKE_REPOSITORY, OS999_DEPS_STORE), never versions.

    gate_pins.py preflight --repo CANDIDATE --out RECEIPT_DIR

Writes pins.json (provenance) and pins.env (shell exports) into RECEIPT_DIR,
or prints every problem and exits 2 before any expensive stage runs.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

DEFAULT_STORE = Path.home() / 'Library/Application Support/OS999/Integration/deps'
HEX40 = re.compile(r'[0-9a-f]{40}')
BRIDGE_HOST, BRIDGE_ENGINE = 'include/os999_quake_bridge.h', 'code/darwin/os999_bridge.h'


class PinError(Exception):
    pass


def read_pins(path):
    """KEY=VALUE lines; comments and blanks skipped; duplicates rejected."""
    try:
        text = Path(path).read_text()
    except OSError as error:
        raise PinError(f'cannot read pin file {path}: {error.strerror}')
    pins = {}
    for number, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        if '=' not in line:
            raise PinError(f'{path}:{number}: expected KEY=VALUE')
        key, value = line.split('=', 1)
        if key in pins:
            raise PinError(f'{path}:{number}: duplicate {key}')
        pins[key.strip()] = value.strip()
    return pins


def script_pin(path, names):
    """Read name=value shell assignments (SDL2 and Musashi pins live in their scripts)."""
    text = Path(path).read_text()
    out = {}
    for name in names:
        match = re.search(rf'^{name}=([0-9A-Za-z._-]+)$', text, re.M)
        if not match:
            raise PinError(f'{path}: no {name}= pin found')
        out[name] = match.group(1)
    return out


def store_root(env):
    return Path(env.get('OS999_DEPS_STORE') or DEFAULT_STORE)


def resolve_cef(store, pin, errors):
    """Find the pinned CEF in the store: cef/<version>/ first, flat cef-<version> (legacy) second."""
    version, sha1 = pin['CEF_VERSION'], pin['CEF_SHA1_macosarm64']
    tried = []
    for layout, path in (('versioned', store / 'cef' / version), ('legacy-flat', store / ('cef-' + version))):
        tried.append(str(path))
        if not path.is_dir():
            continue
        stamp = path / '.version'
        found = stamp.read_text().strip() if stamp.is_file() else None
        if found != version:
            errors.append(f'CEF store entry {path} has .version {found!r}, expected {version!r}')
            return None
        if not (path / 'include').is_dir():
            errors.append(f'CEF store entry {path} has no include/ directory (incomplete extraction)')
            return None
        marker = path / '.archive-sha1'
        if marker.is_file():
            recorded = marker.read_text().strip()
            if recorded != sha1:
                errors.append(f'CEF store entry {path} was verified against archive SHA-1 {recorded}, '
                              f'but the candidate pins {sha1}')
                return None
            check = 'marker'
        else:
            check = 'legacy-unverified'
        return {'version': version, 'archive_sha1': sha1, 'archive_sha1_check': check,
                'layout': layout, 'path': str(path)}
    errors.append(f'CEF {version} is not in the dependency store. Looked for: {"; ".join(tried)}. '
                  f'Fetch it with: sh browser/scripts/fetch-cef.sh macosarm64 --store {store} '
                  f'(or rerun with OS999_DEPS_FETCH=1)')
    return None


def git(repo, *args):
    return subprocess.run(['git', '-C', str(repo), *args], capture_output=True)


def resolve_quake(repo, pin, env, errors):
    commit = pin.get('QUAKE_COMMIT', '')
    if not HEX40.fullmatch(commit):
        errors.append(f'config/quake-pin.txt QUAKE_COMMIT must be a full 40-hex commit, got {commit!r}')
        return None
    stale = env.get('OS999_QUAKE_REF', '')
    if stale and stale != commit:
        errors.append(f'OS999_QUAKE_REF in the environment ({stale}) differs from the candidate pin '
                      f'config/quake-pin.txt ({commit}). The candidate owns the version: remove '
                      f'OS999_QUAKE_REF from the launch agent.')
        return None
    location = env.get('OS999_QUAKE_REPOSITORY', '')
    if not location:
        errors.append('OS999_QUAKE_REPOSITORY is not set; it names the external engine repository')
        return None
    if git(location, 'rev-parse', '--git-dir').returncode:
        errors.append(f'OS999_QUAKE_REPOSITORY {location} is not a git repository')
        return None
    resolved = git(location, 'rev-parse', '--verify', '--quiet', commit + '^{commit}')
    if resolved.returncode or resolved.stdout.decode().strip() != commit:
        errors.append(f'Quake commit {commit} (candidate pin) does not exist in {location}; fetch it there first')
        return None
    wanted = pin.get('QUAKE_ROOT_COMMITS', '').split()
    if wanted:
        roots = set(git(location, 'rev-list', '--max-parents=0', commit).stdout.decode().split())
        if not roots & set(wanted):
            errors.append(f'{location} does not look like the pinned engine: root commits {sorted(roots)} '
                          f'match none of QUAKE_ROOT_COMMITS')
            return None
    header = git(location, 'show', f'{commit}:{BRIDGE_ENGINE}')
    host = Path(repo) / BRIDGE_HOST
    if header.returncode:
        errors.append(f'Quake commit {commit} has no {BRIDGE_ENGINE}')
        return None
    if header.stdout != host.read_bytes():
        errors.append(f'{BRIDGE_HOST} differs from {BRIDGE_ENGINE} at Quake commit {commit}; '
                      f'bump config/quake-pin.txt together with the bridge header')
        return None
    return {'repository': str(location), 'commit': commit, 'bridge_header_matches': True,
            'env_ref_ignored': not stale}


def preflight(repo, env, fetch=False):
    repo, errors, warnings = Path(repo), [], []
    cef_pin = read_pins(repo / 'browser/cef-pin.txt')
    quake_pin = read_pins(repo / 'config/quake-pin.txt')
    for key in ('CEF_VERSION', 'CEF_SHA1_macosarm64'):
        if not cef_pin.get(key):
            errors.append(f'browser/cef-pin.txt lacks {key}')
    if errors:
        raise PinError('; '.join(errors))
    store = store_root(env)
    stale_root = env.get('OS999_CEF_ROOT', '')
    if stale_root:
        warnings.append(f'OS999_CEF_ROOT={stale_root} in the environment is ignored; '
                        f'CEF is resolved from {store} using the candidate pin. Remove it from the launch agent.')
    cef = resolve_cef(store, cef_pin, [])
    if cef is None and fetch:
        fetched = subprocess.run(['sh', str(repo / 'browser/scripts/fetch-cef.sh'), 'macosarm64',
                                  '--store', str(store)], capture_output=True, text=True)
        if fetched.returncode:
            errors.append('OS999_DEPS_FETCH fetch failed: ' + (fetched.stderr or fetched.stdout).strip()[-400:])
    if cef is None and not errors:
        cef = resolve_cef(store, cef_pin, errors)
    quake = resolve_quake(repo, quake_pin, env, errors)
    sdl2 = script_pin(repo / 'scripts/prepare-sdl2.sh', ('version', 'digest'))
    musashi = script_pin(repo / 'scripts/prepare-musashi.sh', ('revision', 'digest'))
    if errors:
        raise PinError('\n'.join(errors))
    return {'store': str(store), 'cef': cef, 'quake': quake,
            'sdl2': {'version': sdl2['version'], 'archive_sha256': sdl2['digest']},
            'musashi': {'revision': musashi['revision'], 'archive_sha256': musashi['digest']},
            'warnings': warnings}


def write_outputs(pins, out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'pins.json').write_text(json.dumps(pins, indent=2) + '\n')
    exports = {'OS999_DEPS_STORE': pins['store'], 'OS999_CEF_ROOT': pins['cef']['path'],
               'OS999_CEF_VERSION': pins['cef']['version'],
               'OS999_QUAKE_REPOSITORY': pins['quake']['repository'],
               'OS999_QUAKE_REF': pins['quake']['commit']}
    (out / 'pins.env').write_text(''.join(f'export {k}={shlex.quote(v)}\n' for k, v in exports.items()))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest='command', required=True)
    check = sub.add_parser('preflight')
    check.add_argument('--repo', type=Path, default=Path.cwd())
    check.add_argument('--out', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        pins = preflight(args.repo, os.environ, fetch=os.environ.get('OS999_DEPS_FETCH') == '1')
    except PinError as error:
        print('pins: FAILED before any build stage\n' + '\n'.join('  - ' + line
              for line in str(error).splitlines()), file=sys.stderr)
        return 2
    for warning in pins['warnings']:
        print('pins: warning: ' + warning)
    write_outputs(pins, args.out)
    print(f"pins: CEF {pins['cef']['version']} ({pins['cef']['layout']}, SHA-1 {pins['cef']['archive_sha1_check']}) "
          f"at {pins['cef']['path']}; Quake {pins['quake']['commit'][:12]}; "
          f"SDL2 {pins['sdl2']['version']}; Musashi {pins['musashi']['revision'][:12]}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
