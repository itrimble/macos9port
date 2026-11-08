"""Verify byte preservation and rejection of changed authenticated modules."""
import importlib.util
import json
import plistlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('quake_package',
    Path(__file__).resolve().parents[1] / 'scripts/quake_package.py')
Q = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(Q)


class ProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='os999-quake-package-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build, self.dest = self.root / 'build', self.root / 'package'
        (self.build / 'baseq3').mkdir(parents=True)
        self.dest.mkdir()
        source = self.root / 'module.c'
        source.write_text('int vmMain(void) { return 0; }\n')
        module = self.build / 'baseq3/qagamearm64.so'
        subprocess.run(['cc', '-dynamiclib', str(source), '-o', str(module)], check=True)
        subprocess.run(['codesign', '--force', '--sign', '-', '--timestamp=none', str(module)], check=True)
        modules = {}
        for name in ('qagame', 'cgame', 'ui'):
            target = self.build / 'baseq3' / (name + 'arm64.so')
            if target != module:
                shutil.copy2(module, target)
            modules[target.name] = Q.digest(target)
        shutil.copytree(self.build / 'baseq3', self.dest / 'baseq3')
        overlay = self.build / 'arena111-overlay.pk3'
        overlay.write_bytes(b'fixture-overlay')
        self.record = {'profile': 'arena-1.11', 'modules': modules,
                       'overlay': {'file': overlay.name, 'sha256': Q.digest(overlay)}}
        self.write_manifest()

    def write_manifest(self):
        (self.build / 'arena111-package.json').write_text(json.dumps(self.record))

    def test_copy_preserves_signatures_and_exact_bytes(self):
        Q.copy_profile(self.build, self.dest)
        verified = Q.verified_modules(self.dest)
        self.assertEqual(len(verified), 6)
        for path, sha in verified.items():
            self.assertEqual(Q.digest(path), sha)
        with (self.dest / 'demoq3/uiarm64.so').open('ab') as stream:
            stream.write(b'tampered')
        with self.assertRaisesRegex(ValueError, 'module differs'):
            Q.verified_modules(self.dest)

    def test_complete_packaging_preserves_authenticated_bytes(self):
        Q.copy_profile(self.build, self.dest)
        app, browser = self.root / 'Host.app', self.root / 'Browser.app'
        source = self.root / 'main.c'
        source.write_text('int main(void) { return 0; }\n')
        for bundle in (app, browser):
            macos = bundle / 'Contents/MacOS'
            macos.mkdir(parents=True)
            executable = macos / bundle.stem
            subprocess.run(['cc', str(source), '-o', str(executable)], check=True)
            (bundle / 'Contents/Info.plist').write_bytes(plistlib.dumps({
                'CFBundleExecutable': bundle.stem, 'CFBundleIdentifier': 'test.' + bundle.stem,
                'CFBundlePackageType': 'APPL'}))
        resources = app / 'Contents/Resources'
        resources.mkdir()
        shutil.move(self.dest, resources / 'Quake3')
        script = Path(__file__).resolve().parents[1] / 'scripts/complete-package.py'
        subprocess.run(['python3', str(script), str(app), str(browser)], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(len(Q.verified_modules(resources / 'Quake3')), 6)

    def test_rejects_overlay_tampering(self):
        (self.build / 'arena111-overlay.pk3').write_bytes(b'wrong-overlay')
        with self.assertRaisesRegex(ValueError, 'overlay differs'):
            Q.copy_profile(self.build, self.dest)

    def test_rejects_paths_outside_inventory(self):
        self.record['modules']['../uiarm64.so'] = self.record['modules'].pop('uiarm64.so')
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, 'inventory'):
            Q.copy_profile(self.build, self.dest)


if __name__ == '__main__':
    unittest.main()
