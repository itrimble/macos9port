#!/usr/bin/env python3
"""Finish a fresh macOS bundle with the browser and relocatable host libraries."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess

MAGIC = {b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xca\xfe\xba\xbe',
         b'\xfe\xed\xfa\xcf', b'\xbe\xba\xfe\xca', b'\xca\xfe\xba\xbf'}


def run(*args):
    return subprocess.check_output(list(map(str, args)), text=True, stderr=subprocess.STDOUT)


def macho(path):
    if path.is_symlink() or not path.is_file():
        return False
    with path.open('rb') as stream:
        return stream.read(4) in MAGIC


def dependencies(path):
    # cctools treats trailing parentheses as an archive-member expression.
    # CEF helper names contain parentheses; inspect an inherited descriptor.
    with path.open('rb') as stream:
        listing = subprocess.check_output(
            ['/usr/bin/otool', '-L', f'/dev/fd/{stream.fileno()}'],
            pass_fds=(stream.fileno(),), text=True, stderr=subprocess.STDOUT)
        identity = subprocess.check_output(
            ['/usr/bin/otool', '-D', f'/dev/fd/{stream.fileno()}'],
            pass_fds=(stream.fileno(),), text=True, stderr=subprocess.STDOUT).splitlines()[1:]
    names = [line.strip().split(' (compatibility version')[0]
            for line in listing.splitlines()[1:]
            if ' (compatibility version' in line]
    # otool includes LC_ID_DYLIB before dependencies. It is the library's own
    # install name, not another file to resolve (CEF uses libvulkan.dylib).
    if names and identity and names[0] == identity[0].strip():
        names.pop(0)
    return names


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def resolve_dependency(binary, origin, dep):
    """Resolve before relocation, then replace every non-system edge explicitly."""
    if dep.startswith('/'):
        return Path(dep).resolve(strict=True)
    app = next((p for p in origin.parents if p.suffix == '.app'), None)
    executable = app / 'Contents/MacOS' if app else origin.parent
    def expand(value):
        if value == '@loader_path':
            return origin.parent
        if value == '@executable_path':
            return executable
        if value.startswith('@loader_path/'):
            return origin.parent / value[len('@loader_path/'):]
        if value.startswith('@executable_path/'):
            return executable / value[len('@executable_path/'):]
        if value.startswith('/'):
            return Path(value)
        raise ValueError(f'Unsupported loader path in {origin}: {value}')
    if dep.startswith(('@loader_path/', '@executable_path/')):
        return expand(dep).resolve(strict=True)
    if not dep.startswith('@rpath/'):
        raise ValueError(f'Unresolved dependency in {binary}: {dep}')
    with origin.open('rb') as stream:
        commands = subprocess.check_output(
            ['/usr/bin/otool', '-l', f'/dev/fd/{stream.fileno()}'],
            pass_fds=(stream.fileno(),), text=True)
    paths = re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', commands)
    suffix = dep[len('@rpath/'):]
    candidates = [expand(path) / suffix for path in paths]
    # Homebrew libraries can rely on an importing executable's rpath. Resolve
    # an existing sibling from the original installation, never from cwd.
    candidates.append(origin.parent / suffix)
    if app:
        candidates.append(app / 'Contents/Frameworks' / suffix)
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve(strict=True)
    raise ValueError(f'Unresolved rpath in {origin}: {dep}')


def package(app, browser, identity):
    app, browser = app.resolve(strict=True), browser.resolve(strict=True)
    if app.suffix != '.app' or browser.suffix != '.app':
        raise ValueError('Expected existing app bundles')
    processes = run('/bin/ps', '-axo', 'command=')
    if str(app / 'Contents/MacOS') in processes:
        raise ValueError('Target bundle is running; build a new bundle')
    frameworks = app / 'Contents/Frameworks'
    frameworks.mkdir(exist_ok=True)
    target = frameworks / 'OS999Browser.app'
    if target.exists():
        raise ValueError('Browser already packaged; use a fresh app bundle')
    shutil.copytree(browser, target, symlinks=True)
    libraries = frameworks / 'HostLibraries'
    libraries.mkdir(exist_ok=True)
    queue = [p for p in app.rglob('*') if macho(p)]
    originals, manifest = {}, []
    source_paths = {p: p for p in queue}
    # SDL2-compat loads SDL3 with dlopen, so no LC_LOAD_DYLIB edge exists.
    # Its first macOS lookup is @loader_path/libSDL3.dylib.
    if any('sdl2-compat' in dep for binary in queue for dep in dependencies(binary)):
        directory = Path(run('pkg-config', '--variable=libdir', 'sdl3').strip())
        origin = (directory / 'libSDL3.dylib').resolve(strict=True)
        dest = libraries / origin.name
        shutil.copy2(origin, dest)
        dest.chmod(dest.stat().st_mode | 0o200)
        originals[origin.name] = origin
        source_paths[dest] = origin
        queue.append(dest)
        if dest.name != 'libSDL3.dylib':
            (libraries / 'libSDL3.dylib').symlink_to(dest.name)
        manifest.append({'source': str(origin), 'source_sha256': digest(origin),
                         'bundled': str(dest.relative_to(app)), 'reason': 'SDL2-compat dlopen'})
    for binary in queue:
        for dep in dependencies(binary):
            if dep.startswith(('/System/', '/usr/lib/')):
                continue
            origin = resolve_dependency(binary, source_paths[binary], dep)
            if origin.is_relative_to(app):
                relative = '@loader_path/' + os.path.relpath(origin, binary.parent)
                if dep != relative:
                    run('/usr/bin/install_name_tool', '-change', dep, relative, binary)
                continue
            if not macho(origin) or '.framework' in str(origin):
                raise ValueError(f'Unsupported external dependency: {dep}')
            name = origin.name
            if name in originals and originals[name] != origin:
                raise ValueError(f'Conflicting library names: {origin}, {originals[name]}')
            dest = libraries / name
            if name not in originals:
                originals[name] = origin
                shutil.copy2(origin, dest)
                dest.chmod(dest.stat().st_mode | 0o200)
                run('/usr/bin/install_name_tool', '-id', '@rpath/' + name, dest)
                queue.append(dest)
                source_paths[dest] = origin
                manifest.append({'source': str(origin), 'source_sha256': digest(origin),
                                 'bundled': str(dest.relative_to(app))})
            relative = '@loader_path/' + os.path.relpath(dest, binary.parent)
            run('/usr/bin/install_name_tool', '-change', dep, relative, binary)
    for binary in queue:
        for dep in dependencies(binary):
            if dep.startswith('/') and not dep.startswith(('/System/', '/usr/lib/')):
                raise ValueError(f'Unbundled dependency in {binary}: {dep}')
            if dep.startswith('@') and not dep.startswith('@loader_path/'):
                raise ValueError(f'Unresolved loader token in {binary}: {dep}')
            if dep.startswith('@loader_path/'):
                resolved = (binary.parent / dep[len('@loader_path/'):]).resolve()
                if not resolved.is_relative_to(app) or not resolved.exists():
                    raise ValueError(f'Escaping or missing dependency in {binary}: {dep}')
    resources = app / 'Contents/Resources'
    (resources / 'HostLibraries.json').write_text(json.dumps(manifest, indent=2) + '\n')
    # Sign inner code first. Preserve helper sandbox entitlements when present.
    for binary in queue:
        run('/usr/bin/codesign', '--force', '--sign', identity, '--timestamp=none',
            '--preserve-metadata=entitlements', binary)
    bundles = [p for p in app.rglob('*') if p.is_dir() and not p.is_symlink()
               and p.suffix in ('.app', '.framework')]
    for bundle in sorted(bundles, key=lambda p: len(p.parts), reverse=True) + [app]:
        run('/usr/bin/codesign', '--force', '--sign', identity, '--timestamp=none',
            '--preserve-metadata=entitlements', bundle)
    run('/usr/bin/codesign', '--verify', '--deep', '--strict', app)
    print(json.dumps({'artifact': str(app), 'host_libraries': len(manifest),
                      'mach_objects': len(queue), 'signature': 'verified'}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    parser.add_argument('browser', type=Path)
    parser.add_argument('--identity', default='-')
    args = parser.parse_args()
    package(args.app, args.browser, args.identity)
