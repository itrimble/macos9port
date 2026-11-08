"""Real shell exit semantics, historical uncertainty and safe bounded log access."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from build_progress import STAGES, atomic, emit, identity, log_excerpt, progress_for

REPO = Path(__file__).resolve().parents[1]


class ProgressTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='os999-progress-test-')
        self.addCleanup(self.temp.cleanup)
        self.state = Path(self.temp.name)
        self.run = self.state / 'runs/run1'
        self.run.mkdir(parents=True)

    def shell(self, body):
        command = 'set -eu; repo="$1"; OS999_RECEIPT_DIR="$2"; . "$repo/scripts/progress-events.sh"; ' + body
        return subprocess.Popen(['/bin/sh', '-c', command, 'gate', str(REPO), str(self.run)],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)

    def test_failed_command_stops_shell_and_records_failure(self):
        process = self.shell('progress_start sdl; false; touch "$OS999_RECEIPT_DIR/incorrect"')
        self.assertEqual(process.wait(timeout=10), 1)
        self.assertFalse((self.run / 'incorrect').exists())
        value = progress_for(self.run)
        self.assertEqual(value['status'], 'failed')
        self.assertEqual(value['steps'][0]['status'], 'failed')
        self.assertGreaterEqual(value['steps'][0]['duration'], 0)

    def test_signal_marks_active_stage_interrupted(self):
        process = self.shell('progress_start sdl; sleep 30')
        deadline = time.time()+10
        while time.time() < deadline:
            if (self.run / 'progress.json').exists() and json.loads((self.run / 'progress.json').read_text()).get('current'):
                break
            time.sleep(.02)
        os.killpg(process.pid, signal.SIGTERM)
        self.assertEqual(process.wait(timeout=10), 143)
        self.assertIn('SIGTERM', progress_for(self.run)['reason'])

    def test_sourced_exports_survive_and_success_requires_all_steps(self):
        (self.run / 'exports.sh').write_text('export TEST_GATE_VALUE=preserved\n')
        process = self.shell('. "$OS999_RECEIPT_DIR/exports.sh"; test "$TEST_GATE_VALUE" = preserved; progress_start sdl; progress_pass sdl')
        self.assertEqual(process.wait(timeout=10), 1)
        self.assertEqual(progress_for(self.run)['status'], 'failed')
        self.assertIn('every stage', progress_for(self.run)['reason'])

    def test_all_completed_steps_produce_success(self):
        stages = ' '.join(key for key, _ in STAGES)
        process = self.shell('for stage in '+stages+'; do progress_start "$stage"; progress_pass "$stage"; done')
        self.assertEqual(process.wait(timeout=20),0)
        value = progress_for(self.run)
        self.assertEqual(value['status'],'passed')
        self.assertEqual(value['counts']['passed'],len(STAGES))

    def test_skip_is_distinct_and_cannot_erase_active_stage(self):
        emit(self.run, 'init', pid=os.getpid())
        emit(self.run, 'start', step='sdl')
        with self.assertRaises(ValueError):
            emit(self.run, 'skip', step='browser')
        emit(self.run, 'pass', step='sdl')
        emit(self.run, 'skip', step='browser', reason='Explicit fixture unavailable')
        value = progress_for(self.run)
        self.assertEqual(value['counts']['skipped'], 1)
        self.assertEqual(value['counts']['passed'], 1)
        self.assertNotIn('duration', value['steps'][1])
        with self.assertRaises(ValueError): progress_for(self.run, 'wrong-tree')

    def test_stale_identity_not_silent_log_age(self):
        emit(self.run, 'init', pid=os.getpid())
        emit(self.run, 'start', step='sdl')
        value = json.loads((self.run/'progress.json').read_text())
        value['started'] = 1
        atomic(self.run/'progress.json', value)
        self.assertEqual(progress_for(self.run)['status'], 'running')
        value['process_start'] = 'different process'
        atomic(self.run/'progress.json', value)
        stale = progress_for(self.run)
        self.assertEqual(stale['status'], 'failed')
        self.assertNotIn('duration', stale['steps'][0])

    def test_historical_receipt_does_not_invent_times(self):
        atomic(self.run/'receipt.json', {'tree':'exact', 'result':'pass'})
        self.assertIsNone(progress_for(self.run, 'wrong'))
        value = progress_for(self.run, 'exact')
        self.assertTrue(value['historical'])
        self.assertTrue(all('started' not in s and 'duration' not in s for s in value['steps']))

    def test_log_bounds_redaction_and_path_rejection(self):
        emit(self.run, 'init', pid=os.getpid())
        emit(self.run, 'start', step='sdl')
        (self.run/'build.log').write_text('x'*100000+'\nAuthorization: Bearer private-token\napi_key=supersecret\n')
        result = log_excerpt(self.state, 'run1', 'sdl')
        self.assertTrue(result['truncated'])
        self.assertLessEqual(len(result['text']),65536)
        self.assertNotIn('private-token',result['text'])
        self.assertNotIn('supersecret',result['text'])
        with self.assertRaises(ValueError): log_excerpt(self.state,'../run1','sdl')
        with self.assertRaises(ValueError): log_excerpt(self.state,'run1','unknown')
        (self.run/'build.log').unlink()
        (self.state/'secret').write_text('private')
        (self.run/'build.log').symlink_to(self.state/'secret')
        with self.assertRaises(ValueError): log_excerpt(self.state,'run1','sdl')


if __name__ == '__main__':
    unittest.main()
