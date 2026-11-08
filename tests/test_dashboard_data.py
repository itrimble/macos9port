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

if __name__ == '__main__':
    unittest.main()
