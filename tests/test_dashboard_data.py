"""Evidence acceptance boundaries for dashboard data."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from dashboard_data import checklist, collect, source_text

class EvidenceTests(unittest.TestCase):
    def test_acceptance_and_live_revision(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'docs').mkdir()
            (root / 'config').mkdir()
            path = root / 'docs/DEVELOPMENT-CHECKLIST.md'
            path.write_text('- [ ] V01 Open\n- [x] V02 Accepted\n')
            rows = checklist(root, {'task_evidence': {'V01': {'status': 'verified'}}})
            self.assertEqual(rows[0]['status'], 'pending')
            self.assertEqual(rows[1]['status'], 'verified')
            with self.assertRaises(ValueError):
                source_text(root, '../escape', ['../escape'])
            with self.assertRaises(ValueError):
                checklist(root, {'parity_tasks': [{'id': 'V01', 'title': 'Duplicate'}]})
            (root / 'config/dashboard.json').write_text(json.dumps({
                'metrics': [{'baseline': {'value': 12}}]}))
            before = collect(root, root / 'state')
            self.assertIsNone(before['metrics'][0]['baseline'])
            self.assertEqual(before['revision'], collect(root, root / 'state')['revision'])
            path.write_text('- [x] V01 Accepted\n')
            self.assertNotEqual(before['revision'], collect(root, root / 'state')['revision'])

    def test_split_checklists_preserve_multiletter_ids(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'first.md').write_text('- [ ] V01 Existing gate\n')
            (root / 'second.md').write_text('- [ ] CC01 Guest scheduling\n  Evidence pending\n')
            rows = checklist(root, {'checklists':['first.md','second.md']})
            self.assertEqual([row['id'] for row in rows], ['V01','CC01'])
            self.assertEqual(rows[1]['component'], 'Classic compatibility')
            self.assertIn('Evidence pending', rows[1]['detail'])

    def test_manual_validation_visible_without_queue_admission(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'docs').mkdir()
            (root / 'docs/DEVELOPMENT-CHECKLIST.md').write_text('- [ ] V01 Open\n')
            state = root / 'state'
            run = state / 'runs/build9'
            run.mkdir(parents=True)
            record = {'kind': 'manual_snapshot_validation', 'number': 9,
                      'status': 'running', 'source_tree': 'source-snapshot'}
            path = run / 'build-state.json'
            path.write_text(json.dumps(record))
            before = collect(root, state)
            self.assertEqual(before['builds'][0]['status'], 'running')
            self.assertEqual(before['builds'][0]['commit'], '')
            record['status'] = 'validated'
            path.write_text(json.dumps(record))
            after = collect(root, state)
            self.assertEqual(after['builds'][0]['status'], 'validated')
            self.assertNotEqual(before['revision'], after['revision'])
            self.assertFalse((state / 'queue').exists())
            self.assertIsNone(after['latest'])
            (state / 'queue').mkdir()
            (state / 'queue/promoted.json').write_text(json.dumps({'number': 9, 'status': 'merged'}))
            self.assertEqual(collect(root, state)['builds'], [{'number': 9, 'status': 'merged'}])

if __name__ == '__main__':
    unittest.main()
