#!/usr/bin/env python3
"""Exercise queue promotion and refusal using real isolated Git repositories."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('watch', Path(__file__).resolve().parents[1] /
                                             'scripts/integration-watch.py')
watch = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(watch)
watch.notify = lambda message: None


class QueueTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='os999-queue-test-')
        root = Path(self.temp.name)
        self.repo, self.state = root / 'repo', root / 'state'
        self.repo.mkdir()
        for child in ('queue', 'runs'):
            (self.state / child).mkdir(parents=True)
        self.git('init', '-b', 'main')
        self.git('config', 'user.name', 'Fixture')
        self.git('config', 'user.email', 'fixture@example.invalid')
        (self.repo / 'base').write_text('base\n')
        self.git('add', 'base')
        self.git('commit', '-m', 'Base')
        self.base = self.git('rev-parse', 'HEAD')
        self.git('checkout', '-b', 'feature')
        (self.repo / 'feature').write_text('feature\n')
        self.git('add', 'feature')
        self.git('commit', '-m', 'Feature')
        self.feature = self.git('rev-parse', 'HEAD')
        self.git('checkout', 'main')
        self.gate = root / 'gate'
        self.gate.write_text('#!/usr/bin/env python3\nimport os,json,pathlib\n'
                            'p=pathlib.Path(os.environ["OS999_RECEIPT_DIR"])\n'
                            '(p/"App.app").mkdir()\n'
                            '(p/"receipt.json").write_text(json.dumps({'
                            '"tree":os.environ["OS999_SOURCE_TREE"],'
                            '"result":"pass","artifact":str(p/"App.app")}))\n')
        self.gate.chmod(0o755)

    def tearDown(self):
        self.temp.cleanup()

    def git(self, *args):
        return watch.git(self.repo, *args)

    def item(self):
        return json.loads((self.state / 'queue' / (self.feature + '.json')).read_text())

    def submit(self):
        return watch.ready(self.repo, self.state, self.feature, [])

    def run_queue(self):
        watch.process(self.repo, self.state, self.gate)

    def test_promotes_once_and_preserves_source_branch(self):
        self.submit()
        self.run_queue()
        result = self.git('rev-parse', 'main')
        self.assertNotEqual(result, self.base)
        self.assertEqual(self.git('rev-parse', 'feature'), self.feature)
        self.assertEqual((self.repo / 'feature').read_text(), 'feature\n')
        self.assertEqual(self.item()['status'], 'merged')
        self.submit()
        self.run_queue()
        self.assertEqual(self.git('rev-parse', 'main'), result)
        self.assertEqual(self.item()['attempts'], 1)
        self.assertNotIn('Co-Authored-By', self.git('log', '-1', '--format=%B'))

    def test_dirty_main_is_untouched(self):
        self.submit()
        (self.repo / 'base').write_text('user work\n')
        self.run_queue()
        self.assertEqual(self.git('rev-parse', 'main'), self.base)
        self.assertEqual((self.repo / 'base').read_text(), 'user work\n')
        self.assertEqual(self.item()['attempts'], 0)

    def test_failed_gate_retains_candidate_and_main(self):
        self.submit()
        self.gate.write_text('#!/bin/sh\nexit 7\n')
        self.run_queue()
        self.assertEqual(self.git('rev-parse', 'main'), self.base)
        self.assertEqual(self.item()['status'], 'failed')
        self.assertTrue((Path(self.item()['run']) / 'source' / 'feature').exists())

    def test_success_without_receipt_cannot_promote(self):
        self.submit()
        self.gate.write_text('#!/bin/sh\nexit 0\n')
        self.run_queue()
        self.assertEqual(self.git('rev-parse', 'main'), self.base)
        self.assertEqual(self.item()['status'], 'failed')

    def test_gate_untracked_source_refuses_promotion(self):
        self.submit()
        with self.gate.open('a') as stream:
            stream.write('import sys\n(pathlib.Path(sys.argv[1])/"injected.c").write_text("changed")\n')
        self.run_queue()
        self.assertEqual(self.item()['status'], 'failed')
        self.assertEqual(self.git('rev-parse', 'main'), self.base)

    def test_retry_allocates_new_number(self):
        self.submit()
        self.gate.write_text('#!/bin/sh\nexit 7\n')
        self.run_queue()
        first = self.item()
        first['status'] = 'ready'
        watch.write_json(self.state / 'queue' / (self.feature + '.json'), first)
        self.run_queue()
        self.assertEqual(self.item()['number'], first['number'] + 1)
        self.assertNotEqual(self.item()['run'], first['run'])
        self.assertTrue(Path(first['run']).exists())

    def test_watch_exposes_dirty_main_without_building(self):
        self.submit()
        (self.repo / 'base').write_text('unsaved')
        status = watch.watch_cycle(self.repo, self.state, self.gate)
        self.assertEqual(status['status'], 'waiting')
        self.assertIn('dirty', status['reason'])
        self.assertEqual(self.item()['attempts'], 0)
        self.assertEqual(json.loads((self.state / 'worker.json').read_text()), status)

    def test_untracked_content_changes_fingerprint(self):
        path = self.repo / 'new'
        path.write_text('one')
        before = watch.fingerprint(self.repo)
        path.write_text('two')
        self.assertNotEqual(watch.fingerprint(self.repo), before)

    def test_watch_uses_source_build_inputs_without_prebuilt_browser(self):
        self.submit()
        names = ('OS999_CEF_ROOT', 'OS999_QUAKE_REPOSITORY', 'OS999_QUAKE_REF')
        with patch.dict(os.environ, {name: '' for name in names}):
            status = watch.watch_cycle(self.repo, self.state, self.gate)
            self.assertEqual(status['status'], 'waiting')
            self.assertIn('build inputs', status['reason'])
            self.assertEqual(self.item()['attempts'], 0)
        with patch.dict(os.environ, {name: 'fixture' for name in names}):
            os.environ.pop('OS999_BROWSER_BUNDLE', None)
            status = watch.watch_cycle(self.repo, self.state, self.gate)
            self.assertEqual(self.item()['status'], 'merged')

    def test_pause_and_dependencies(self):
        watch.ready(self.repo, self.state, self.feature, [self.feature])
        self.run_queue()
        self.assertEqual(self.item()['attempts'], 0)
        (self.state / 'paused').touch()
        self.run_queue()
        self.assertEqual(self.git('rev-parse', 'main'), self.base)


if __name__ == '__main__':
    unittest.main()
