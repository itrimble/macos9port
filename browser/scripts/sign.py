#!/usr/bin/env python3
"""Sign only a completed, stopped development bundle; not release notarization."""
import argparse
import os
from pathlib import Path
import subprocess

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    parser.add_argument('--identity', default=os.environ.get('OS999_CODESIGN_IDENTITY', '-'))
    args = parser.parse_args()
    app = args.app.resolve(strict=True)
    framework = app/'Contents/Frameworks/Chromium Embedded Framework.framework'
    helpers = sorted((app/'Contents/Frameworks').glob('*.app'))
    # Chromium serializes the cached plist for dynamic process validation.
    # The signed bytes must match Foundation's canonical XML serialization.
    for bundle in [app, *helpers]:
        subprocess.run(['/usr/bin/plutil', '-convert', 'xml1',
                        str(bundle/'Contents/Info.plist')], check=True)
    def sign(path):
        subprocess.run(['codesign', '--force', '--sign', args.identity,
                        '--timestamp=none', str(path)], check=True)
    for library in sorted((framework/'Versions/A/Libraries').glob('*.dylib')):
        sign(library)
    sign(framework)
    for helper in helpers:
        sign(helper)
    sign(app)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(app)], check=True)

if __name__ == '__main__':
    main()
