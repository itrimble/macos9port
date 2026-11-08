"""Exercise live HTTP delivery and recovery without a browser or production edits."""
import http.client
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from http.server import ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
spec = importlib.util.spec_from_file_location('dashboard', ROOT / 'scripts/os999-dashboard.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ServiceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        for folder in ('config', 'docs', 'scripts', 'assets/dashboard'):
            (self.root / folder).mkdir(parents=True)
        self.config = self.root / 'config/dashboard.json'
        self.config.write_text('{}')
        self.checklist = self.root / 'docs/DEVELOPMENT-CHECKLIST.md'
        self.checklist.write_text('- [ ] V01 Launch Finder\n')
        (self.root / 'scripts/integration-watch.py').write_text(
            (ROOT / 'scripts/integration-watch.py').read_text())
        (self.root / 'assets/dashboard/index.html').write_text('<h1>OS999</h1>')
        self.dashboard = module.Dashboard(self.root, self.root / 'state')
        self.dashboard.refresh()
        self.server = ThreadingHTTPServer(('127.0.0.1', 0), module.handler_for(self.dashboard))
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.dashboard.stop.set()
        with self.dashboard.condition:
            self.dashboard.condition.notify_all()
        self.server.shutdown()
        self.server.server_close()
        self.temp.cleanup()

    def request(self, path, headers=None):
        client = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=3)
        client.request('GET', path, headers=headers or {})
        response = client.getresponse()
        result = response.status, response.read()
        client.close()
        return result

    def test_live_state_and_source_boundaries(self):
        status, body = self.request('/api/state')
        self.assertEqual(status, 200)
        before = json.loads(body)
        self.checklist.write_text('- [x] V01 Launch Finder\n')
        self.dashboard.refresh()
        after = json.loads(self.request('/api/state')[1])
        self.assertNotEqual(before['revision'], after['revision'])
        self.assertEqual(after['counts']['verified'], 1)
        self.assertEqual(self.request('/api/source?path=docs%2FDEVELOPMENT-CHECKLIST.md')[0], 200)
        self.assertEqual(self.request('/api/source?path=..%2Fsecret')[0], 404)
        self.assertEqual(self.request('/%2e%2e/config/dashboard.json')[0], 404)
        self.assertEqual(self.request('/', {'Host': 'foreign.example'})[0], 403)

    def test_invalid_config_preserves_last_snapshot_and_recovers(self):
        revision = self.dashboard.data['revision']
        for invalid in ('{', '[]', '{"metrics": [3]}'):
            self.config.write_text(invalid)
            self.dashboard.refresh()
            self.assertIn('data_error', self.dashboard.data)
            self.assertEqual(self.dashboard.data['total'], 1)
        self.config.write_text('{}')
        self.dashboard.refresh()
        self.assertNotIn('data_error', self.dashboard.data)
        self.assertEqual(self.dashboard.data['revision'], revision)

    def test_milestone_queues_once_only_after_acceptance(self):
        revision = 'a' * 40
        calls = []
        def ready(repo, state, commit, dependencies, title):
            calls.append((commit, title))
            (state / 'queue' / (commit + '.json')).write_text(json.dumps(
                {'commit': commit, 'status': 'ready', 'title': title}))
        self.dashboard.watcher.ready = ready
        self.config.write_text(json.dumps({'automation_enabled': True, 'milestones': [
            {'id': 'finder', 'title': 'Finder integration', 'tasks': ['V01'],
             'ready_commit': revision}]}))
        self.dashboard.refresh()
        self.assertEqual(calls, [])
        self.checklist.write_text('- [x] V01 Launch Finder\n')
        self.dashboard.refresh()
        self.dashboard.automation_thread.join(timeout=3)
        self.dashboard.refresh()
        self.assertEqual(calls, [(revision, 'Finder integration')])

    def test_event_stream_emits_snapshot(self):
        client = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=3)
        client.request('GET', '/api/events')
        response = client.getresponse()
        self.assertEqual(response.getheader('Content-Type'), 'text/event-stream')
        line = response.readline().decode()
        self.assertEqual(json.loads(line.removeprefix('data: '))['total'], 1)
        client.close()


if __name__ == '__main__':
    unittest.main()
