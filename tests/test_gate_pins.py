#!/usr/bin/env python3
"""Pin resolution for the integration gate, against temp trees, a fake CEF
store and a tiny git repository. No real CEF or Quake download happens."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('gate_pins', ROOT / 'scripts/gate_pins.py')
pins = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pins)

V1 = '156.0.15+gold+chromium-156.0.1'
V2 = '156.0.26+gnew+chromium-156.0.2'
SHA1_A, SHA1_B = 'a' * 40, 'b' * 40
HEADER_1, HEADER_2 = b'/* bridge v1 */\n', b'/* bridge v2 */\n'


def run(cwd, *args):
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


class GatePinTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='os999-gate-pins-')
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.cand, self.store, self.out, self.quake = (root / n for n in ('cand', 'store', 'out', 'quake'))
        for d in ('browser', 'config', 'include', 'scripts'):
            (self.cand / d).mkdir(parents=True)
        for name in ('prepare-sdl2.sh', 'prepare-musashi.sh'):
            shutil.copy(ROOT / 'scripts' / name, self.cand / 'scripts' / name)
        self.quake.mkdir()
        (self.quake / 'code/darwin').mkdir(parents=True)
        run(self.quake, 'git', 'init', '-q', '-b', 'main')
        for key, value in (('user.name', 'F'), ('user.email', 'f@example.invalid')):
            run(self.quake, 'git', 'config', key, value)
        (self.quake / 'code/darwin/os999_bridge.h').write_bytes(HEADER_1)
        run(self.quake, 'git', 'add', '-A')
        run(self.quake, 'git', 'commit', '-q', '-m', 'one')
        self.commit1 = run(self.quake, 'git', 'rev-parse', 'HEAD')
        (self.quake / 'code/darwin/os999_bridge.h').write_bytes(HEADER_2)
        run(self.quake, 'git', 'commit', '-q', '-am', 'two')
        self.commit2 = run(self.quake, 'git', 'rev-parse', 'HEAD')
        self.env = {'OS999_DEPS_STORE': str(self.store), 'OS999_QUAKE_REPOSITORY': str(self.quake)}
        self.set_pins(V1, SHA1_A, self.commit1, HEADER_1)
        self.make_cef(V1)

    def set_pins(self, version, sha1, commit, header):
        (self.cand / 'browser/cef-pin.txt').write_text(
            f'# test pin\nCEF_VERSION={version}\nCEF_RELEASE_DATE=2026-01-01\nCEF_SHA1_macosarm64={sha1}\n')
        (self.cand / 'config/quake-pin.txt').write_text(f'QUAKE_COMMIT={commit}\n')
        (self.cand / 'include/os999_quake_bridge.h').write_bytes(header)

    def make_cef(self, version, layout='versioned', marker=None, stamp=None):
        path = self.store / 'cef' / version if layout == 'versioned' else self.store / ('cef-' + version)
        (path / 'include').mkdir(parents=True)
        (path / '.version').write_text(version if stamp is None else stamp)
        if marker:
            (path / '.archive-sha1').write_text(marker)
        return path

    def check(self, **env):
        merged = dict(self.env, **env)
        return pins.preflight(self.cand, merged)

    def fails(self, *needles, **env):
        with self.assertRaises(pins.PinError) as caught:
            self.check(**env)
        for needle in needles:
            self.assertIn(needle, str(caught.exception))
        return str(caught.exception)

    # --- resolution ---
    def test_versioned_store_resolves_and_records_provenance(self):
        got = self.check()
        self.assertEqual(got['cef']['path'], str(self.store / 'cef' / V1))
        self.assertEqual(got['cef']['archive_sha1'], SHA1_A)
        self.assertEqual(got['quake']['commit'], self.commit1)
        self.assertTrue(got['quake']['bridge_header_matches'])
        self.assertEqual(got['sdl2']['version'], '2.32.10')
        self.assertEqual(len(got['musashi']['archive_sha256']), 64)
        pins.write_outputs(got, self.out)
        self.assertEqual(json.loads((self.out / 'pins.json').read_text())['cef']['version'], V1)
        env = (self.out / 'pins.env').read_text()
        self.assertIn(f'export OS999_QUAKE_REF={self.commit1}', env)
        self.assertIn(str(self.store / 'cef' / V1), env)

    def test_legacy_flat_directory_is_accepted_and_flagged_unverified(self):
        shutil.rmtree(self.store / 'cef')
        legacy = self.make_cef(V1, layout='legacy-flat')
        got = self.check()
        self.assertEqual(got['cef']['path'], str(legacy))
        self.assertEqual(got['cef']['layout'], 'legacy-flat')
        self.assertEqual(got['cef']['archive_sha1_check'], 'legacy-unverified')

    def test_candidate_pin_change_picks_the_new_version(self):
        self.make_cef(V2, marker=SHA1_B)
        self.set_pins(V2, SHA1_B, self.commit2, HEADER_2)
        got = self.check()
        self.assertEqual(got['cef']['version'], V2)
        self.assertEqual(got['cef']['path'], str(self.store / 'cef' / V2))
        self.assertEqual(got['cef']['archive_sha1_check'], 'marker')
        self.assertEqual(got['quake']['commit'], self.commit2)

    # --- stale watcher environment ---
    def test_stale_cef_root_env_is_ignored_with_warning(self):
        self.make_cef(V2)
        self.set_pins(V2, SHA1_B, self.commit1, HEADER_1)
        got = self.check(OS999_CEF_ROOT=str(self.store / 'cef' / V1))
        self.assertEqual(got['cef']['path'], str(self.store / 'cef' / V2))
        self.assertIn('OS999_CEF_ROOT', got['warnings'][0])

    def test_stale_quake_ref_env_fails_naming_both(self):
        message = self.fails(self.commit1, self.commit2, OS999_QUAKE_REF=self.commit2)
        self.assertIn('OS999_QUAKE_REF', message)

    def test_matching_quake_ref_env_is_accepted(self):
        self.assertTrue(self.check(OS999_QUAKE_REF=self.commit1)['quake']['commit'])

    # --- preflight failures ---
    def test_missing_store_entry_fails_with_actionable_message(self):
        self.set_pins(V2, SHA1_B, self.commit2, HEADER_2)
        message = self.fails(V2, 'not in the dependency store', 'fetch-cef.sh', str(self.store))
        self.assertNotIn('build/deps', message)

    def test_never_falls_back_to_build_deps(self):
        (self.cand / 'build/deps/cef/include').mkdir(parents=True)
        (self.cand / 'build/deps/cef/.version').write_text(V2)
        self.set_pins(V2, SHA1_B, self.commit2, HEADER_2)
        self.fails('not in the dependency store')

    def test_store_entry_with_wrong_version_stamp_fails(self):
        shutil.rmtree(self.store / 'cef')
        self.make_cef(V1, stamp='155.0.0+gx')
        self.fails('155.0.0+gx', V1)

    def test_archive_marker_mismatch_fails(self):
        shutil.rmtree(self.store / 'cef')
        self.make_cef(V1, marker=SHA1_B)
        self.fails(SHA1_A, SHA1_B)

    def test_quake_commit_missing_from_repository(self):
        self.set_pins(V1, SHA1_A, 'c' * 40, HEADER_1)
        self.fails('does not exist', 'c' * 40)

    def test_quake_bridge_header_mismatch(self):
        self.set_pins(V1, SHA1_A, self.commit1, HEADER_2)
        self.fails('differs', 'os999_bridge.h')

    def test_short_quake_commit_rejected(self):
        self.set_pins(V1, SHA1_A, self.commit1[:12], HEADER_1)
        self.fails('40-hex')

    def test_missing_quake_repository(self):
        with self.assertRaises(pins.PinError) as caught:
            pins.preflight(self.cand, {'OS999_DEPS_STORE': str(self.store)})
        self.assertIn('OS999_QUAKE_REPOSITORY', str(caught.exception))

    def test_root_commit_identity_check(self):
        root = run(self.quake, 'git', 'rev-list', '--max-parents=0', 'HEAD')
        self.set_pins(V1, SHA1_A, self.commit2, HEADER_2)
        pin = self.cand / 'config/quake-pin.txt'
        pin.write_text(pin.read_text() + f'QUAKE_ROOT_COMMITS={root}\n')
        self.assertTrue(self.check())
        pin.write_text(pin.read_text().replace(root, 'd' * 40))
        self.fails('does not look like the pinned engine')

    def test_all_problems_are_reported_together(self):
        self.set_pins(V2, SHA1_B, 'c' * 40, HEADER_1)
        message = self.fails(V2, 'c' * 40)
        self.assertEqual(len(message.splitlines()), 2)

    def test_cli_exits_before_writing_anything_on_failure(self):
        self.set_pins(V2, SHA1_B, self.commit2, HEADER_2)
        env = dict(os.environ, **self.env)
        env.pop('OS999_DEPS_FETCH', None)
        done = subprocess.run(['python3', str(ROOT / 'scripts/gate_pins.py'), 'preflight',
                               '--repo', str(self.cand), '--out', str(self.out)],
                              env=env, capture_output=True, text=True)
        self.assertEqual(done.returncode, 2)
        self.assertIn('FAILED before any build stage', done.stderr)
        self.assertFalse((self.out / 'pins.json').exists())


class RepositoryWiringTests(unittest.TestCase):
    """The real tree: pins are candidate-owned and the gate consumes only them."""

    def test_real_pins_parse(self):
        cef = pins.read_pins(ROOT / 'browser/cef-pin.txt')
        for key in ('CEF_VERSION', 'CEF_SHA1_macosarm64', 'CEF_SHA1_linuxarm64', 'CEF_SHA1_linux64'):
            self.assertTrue(cef[key], key)
        self.assertRegex(cef['CEF_SHA1_macosarm64'], '^[0-9a-f]{40}$')
        self.assertRegex(pins.read_pins(ROOT / 'config/quake-pin.txt')['QUAKE_COMMIT'], '^[0-9a-f]{40}$')
        self.assertEqual(pins.script_pin(ROOT / 'scripts/prepare-sdl2.sh', ('version',))['version'], '2.32.10')

    def test_fetch_script_has_no_private_sha1_copy(self):
        text = (ROOT / 'browser/scripts/fetch-cef.sh').read_text()
        self.assertNotRegex(text, '[0-9a-f]{40}')
        subprocess.check_call(['sh', '-n', str(ROOT / 'browser/scripts/fetch-cef.sh')])

    def test_gate_runs_pins_first_and_ignores_env_versions(self):
        gate = (ROOT / 'scripts/integration-gate.sh').read_text()
        self.assertLess(gate.index('progress_start pins'), gate.index('package-inputs.sh'))
        self.assertIn('pins.json', gate)
        inputs = (ROOT / 'scripts/package-inputs.sh').read_text()
        self.assertNotIn("Path('browser/cef-pin.txt')", inputs)
        self.assertNotIn('rev-parse', inputs)


if __name__ == '__main__':
    unittest.main()
