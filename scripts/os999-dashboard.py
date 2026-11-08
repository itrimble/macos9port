#!/usr/bin/env python3
"""Loopback dashboard: live checklist/Git/receipts and explicit milestone queueing."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import json
import mimetypes
import re
from pathlib import Path
import threading
import time
from urllib.parse import parse_qs, unquote, urlsplit

from dashboard_data import collect, load_json, source_text


class Dashboard:
    def __init__(self, repo, state, interval=3):
        self.repo, self.state, self.interval = repo.resolve(), state.resolve(), interval
        self.data = None
        self.condition = threading.Condition()
        self.stop = threading.Event()
        self.error = None
        self.automation_thread = None
        for child in ('queue', 'runs'):
            (self.state / child).mkdir(parents=True, exist_ok=True)
        spec = importlib.util.spec_from_file_location('os999_watch', repo / 'scripts/integration-watch.py')
        self.watcher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.watcher)

    def refresh(self):
        try:
            data = collect(self.repo, self.state)
            config = load_json(self.repo / 'config/dashboard.json', {})
            # A milestone never silently approves a dirty tree or guesses a revision.
            if config.get('automation_enabled'):
                tasks = {task['id']: task for task in data['tasks']}
                candidates = []
                for milestone in config.get('milestones', []):
                    revision = milestone.get('ready_commit')
                    if revision and not re.fullmatch(r'[0-9a-f]{40}', revision):
                        raise ValueError('Milestone ready_commit must be an exact 40-character commit ID')
                    members = milestone.get('tasks', [])
                    if revision and (self.state / 'queue' / (revision + '.json')).exists():
                        continue
                    if revision and members and all(tasks.get(code, {}).get('status') == 'verified'
                                                    for code in members):
                        candidates.append((revision, milestone.get('depends_on_commits', []), milestone['title']))
                if candidates and (self.automation_thread is None or not self.automation_thread.is_alive()):
                    self.automation_thread = threading.Thread(
                        target=self.queue_milestones, args=(candidates,), daemon=True)
                    self.automation_thread.start()
            self.error = None
        except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
            self.error = str(error)
            data = dict(self.data or {'tasks': [], 'alerts': []})
            data['data_error'] = self.error
            data['revision'] = 'error-' + str(hash(self.error))
        with self.condition:
            self.data = data
            self.condition.notify_all()

    def queue_milestones(self, candidates):
        for revision, dependencies, title in candidates:
            if self.stop.is_set():
                return
            try:
                self.watcher.ready(self.repo, self.state, revision, dependencies, title)
            except (OSError, ValueError, RuntimeError) as error:
                print(f'Milestone queue failed: {error}', flush=True)

    def monitor(self):
        while not self.stop.is_set():
            self.refresh()
            self.stop.wait(self.interval)


def handler_for(dashboard):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'

        def log_message(self, format, *args):
            if args and str(args[0]).startswith('GET /api/events'):
                return
            super().log_message(format, *args)

        def respond(self, status, data, mime='application/json; charset=utf-8'):
            if isinstance(data, str):
                data = data.encode()
            self.send_response(status)
            self.send_header('Content-Type', mime)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self'; "
                             "style-src 'self' 'unsafe-inline'; img-src 'self' data:; "
                             "connect-src 'self'; frame-ancestors 'none'; object-src 'none'")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            port = self.server.server_port
            if self.headers.get('Host') not in (f'127.0.0.1:{port}', f'localhost:{port}'):
                self.respond(403, '{"error":"Loopback host required"}')
                return
            url = urlsplit(self.path)
            if url.path == '/api/state':
                self.respond(200, json.dumps(dashboard.data))
                return
            if url.path == '/api/events':
                self.send_response(200)
                self.send_header('Content-Type', 'text/event-stream')
                self.send_header('Cache-Control', 'no-cache')
                self.send_header('Connection', 'close')
                self.end_headers()
                previous = None
                try:
                    while not dashboard.stop.is_set():
                        with dashboard.condition:
                            data = dashboard.data
                            revision = data.get('revision') if data else None
                            if revision == previous:
                                dashboard.condition.wait(timeout=12)
                        if revision != previous:
                            payload = 'data: ' + json.dumps(data) + '\n\n'
                            previous = revision
                        else:
                            payload = ': heartbeat\n\n'
                        self.wfile.write(payload.encode())
                        self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError):
                    pass
                self.close_connection = True
                return
            if url.path == '/api/source':
                try:
                    name = parse_qs(url.query).get('path', [''])[0]
                    text = source_text(dashboard.repo, name, dashboard.data.get('sources', []))
                    self.respond(200, text, 'text/plain; charset=utf-8')
                except (ValueError, OSError) as error:
                    self.respond(404, json.dumps({'error': str(error)}))
                return
            if url.path == '/vendor/mermaid.js':
                vendor = dashboard.repo / 'build/dashboard-web/mermaid.js'
                if vendor.is_file():
                    self.respond(200, vendor.read_bytes(), 'text/javascript; charset=utf-8')
                else:
                    self.respond(404, '{"error":"Run scripts/prepare-dashboard.sh"}')
                return
            if url.path == '/mermaid.js':
                bundle = dashboard.repo / 'build/dashboard-web/mermaid.js'
                if bundle.is_file():
                    self.respond(200, bundle.read_bytes(), 'text/javascript; charset=utf-8')
                else:
                    self.respond(404, '{"error":"Build the local Mermaid bundle first"}')
                return
            base = (dashboard.repo / 'assets/dashboard').resolve()
            path = (base / unquote(url.path).lstrip('/')).resolve()
            if url.path == '/':
                path = base / 'index.html'
            if not path.is_relative_to(base) or any(part.startswith('.') for part in path.relative_to(base).parts):
                self.respond(404, '{"error":"Not found"}')
            elif path.is_file() and path.suffix in ('.html', '.js', '.css', '.woff2', '.ttf', '.svg'):
                self.respond(200, path.read_bytes(), mimetypes.guess_type(str(path))[0] or 'application/octet-stream')
            else:
                self.respond(404, '{"error":"Not found"}')
    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--state', type=Path, default=Path.home() / 'Library/Application Support/OS999/Integration')
    parser.add_argument('--port', type=int, default=8080)
    args = parser.parse_args()
    dashboard = Dashboard(args.repo, args.state)
    dashboard.refresh()
    server = ThreadingHTTPServer(('127.0.0.1', args.port), handler_for(dashboard))
    thread = threading.Thread(target=dashboard.monitor, daemon=True)
    thread.start()
    print(f'OS999 dashboard: http://127.0.0.1:{args.port}', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        dashboard.stop.set()
        with dashboard.condition:
            dashboard.condition.notify_all()
        server.server_close()


if __name__ == '__main__':
    main()
