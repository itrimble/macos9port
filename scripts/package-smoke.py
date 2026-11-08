#!/usr/bin/env python3
"""Bounded relocated launch; preserve the copied app and disposable volume."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def smoke(app, output):
    app = app.resolve(strict=True)
    output.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='os999-package-smoke-', dir='/tmp'))
    copied = root / app.name
    receipt = {'source': str(app), 'copy': str(copied), 'volume': str(root / 'Volume'),
               'result': 'fail', 'scope': 'Relocated loader and volume seed; no GUI acceptance'}
    try:
        shutil.copytree(app, copied, symlinks=True)
        subprocess.run(['/usr/bin/codesign', '--verify', '--deep', '--strict', str(copied)],
                       check=True, capture_output=True, timeout=30)
        env = {key: value for key, value in os.environ.items() if not key.startswith('DYLD_')}
        env['PATH'] = '/usr/bin:/bin'
        env['DYLD_PRINT_LIBRARIES'] = '1'
        run = subprocess.run([str(copied / 'Contents/MacOS/ospi'), '--seed', receipt['volume']],
                             cwd='/tmp', env=env, capture_output=True, text=True, timeout=30)
        (output / 'relocated-loader.log').write_text(run.stdout + run.stderr)
        receipt['exit_code'] = run.returncode
        if run.returncode:
            raise RuntimeError(f'Relocated seed exited {run.returncode}')
        loaded = [line.split('> ', 1)[1] for line in run.stderr.splitlines()
                  if line.startswith('dyld[') and '> /' in line]
        if not loaded:
            raise RuntimeError('No dynamic-loader evidence captured')
        external = [name for name in loaded if not name.startswith(('/System/', '/usr/lib/'))
                    and not Path(name).resolve().is_relative_to(copied.resolve())]
        if external:
            raise RuntimeError('External runtime libraries: ' + ', '.join(external))
        receipt['loaded_images'] = loaded
        receipt['result'] = 'pass'
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        receipt['error'] = str(error)
    (output / 'relocation.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt))
    return receipt['result'] == 'pass'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    raise SystemExit(0 if smoke(args.app, args.output) else 1)
