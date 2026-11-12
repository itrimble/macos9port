"""Atomic gate events and read-only, receipt-bound dashboard progress."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

STAGES = [('pins', 'Validate candidate pins'), ('sdl', 'Build private SDL2'), ('browser', 'Build and verify Chromium helper'),
          ('quake', 'Build signed Quake modules and engine'), ('provenance', 'Verify build inputs'),
          ('package_tests', 'Packaging regression tests'), ('desktop', 'Desktop, runtime and storage tests'),
          ('tailnet', 'Tailnet tests'), ('tls', 'TLS integration tests'),
          ('keyboard', 'Keyboard and SDL tests'), ('mobile', 'Mobile tests and simulator build'),
          ('links', 'Verify SDL linkage'), ('app', 'Assemble application bundle'),
          ('libraries', 'Bundle libraries and verify signatures'),
          ('relocation', 'Copied-location launch and seed'), ('receipt', 'Write artifact receipt')]


def read(path):
    if path.stat().st_size > 2_000_000:
        raise ValueError('Progress record exceeds limit')
    with path.open() as stream:
        return json.load(stream)


def atomic(path, value):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def identity(pid):
    try:
        value = subprocess.check_output(['/bin/ps', '-p', str(pid), '-o', 'lstart=', '-o', 'stat='],
                                        text=True, stderr=subprocess.DEVNULL).strip()
        started, status = value.rsplit(None, 1)
        return '' if 'Z' in status else started
    except (subprocess.CalledProcessError, ValueError):
        return ''


def offset(run):
    try:
        return (run / 'build.log').stat().st_size
    except FileNotFoundError:
        return 0


def emit(run, action, step=None, code=0, reason='', pid=None):
    run.mkdir(parents=True, exist_ok=True)
    path = run / 'progress.json'
    now = time.time()
    if action == 'init':
        if path.exists():
            raise ValueError('Progress already exists; use a fresh run')
        value = {'version': 1, 'historical': False, 'status': 'running', 'started': now,
                 'pid': pid, 'process_start': identity(pid), 'current': None,
                 'source_tree': os.environ.get('OS999_SOURCE_TREE'),
                 'steps': [{'id': key, 'label': label, 'status': 'pending'} for key, label in STAGES]}
        if not value['process_start']:
            raise ValueError('Cannot identify gate process')
    else:
        value = read(path)
        if action in ('start', 'pass', 'skip'):
            item = next(s for s in value['steps'] if s['id'] == step)
            if action == 'start':
                if value['current'] or item['status'] != 'pending':
                    raise ValueError('Overlapping or repeated stage')
                item.update(status='running', started=now, log_start=offset(run))
                value['current'] = step
            else:
                if action == 'pass' and item['status'] != 'running':
                    raise ValueError('Cannot pass a stage that is not running')
                if action == 'skip' and (item['status'] != 'pending' or value['current']):
                    raise ValueError('Can only skip a pending stage with no active stage')
                item.update(status='passed' if action == 'pass' else 'skipped', ended=now,
                            log_end=offset(run), reason=reason)
                if 'started' in item:
                    item['duration'] = now - item['started']
                value['current'] = None
        elif action == 'exit':
            if value['status'] != 'running':
                return
            active = next((s for s in value['steps'] if s['id'] == value['current']), None)
            if code == 0 and (active or any(s['status'] == 'pending' for s in value['steps'])):
                code, reason = 1, 'Gate exited before every stage finished'
            if active:
                active.update(status='failed', ended=now, duration=now-active['started'],
                              log_end=offset(run), reason=reason or f'Command exited with status {code}')
            value.update(status='passed' if code == 0 else 'failed', ended=now,
                         exit_code=code, reason=reason or (f'Gate exited with status {code}' if code else ''),
                         current=None)
    atomic(path, value)
    return code if action == 'exit' else 0


def progress_for(run, source_tree=None):
    path = run / 'progress.json'
    if path.is_file():
        value = read(path)
        if source_tree and value.get('source_tree') != source_tree:
            raise ValueError('Progress source tree does not match build')
        if value.get('status') == 'running' and identity(value.get('pid')) != value.get('process_start'):
            value.update(status='failed', stale=True, reason='Gate process disappeared or PID identity changed; completion time unknown')
            for step in value['steps']:
                if step['status'] == 'running':
                    step.update(status='failed', reason=value['reason'])
            value['current'] = None
    else:
        receipt = run / 'receipt.json'
        if not receipt.is_file():
            return None
        receipt = read(receipt)
        if receipt.get('result') != 'pass' or (source_tree and receipt.get('tree') != source_tree):
            return None
        value = {'version': 1, 'historical': True, 'status': 'passed', 'current': None,
                 'evidence': 'Reconstructed from the matching successful full-gate receipt. Individual timings were not recorded.',
                 'steps': [{'id': key, 'label': label, 'status': 'passed'} for key, label in STAGES]}
        bridge = run / 'packaged-quake-bridge.json'
        if bridge.is_file():
            probe = read(bridge)
            value['steps'].append({'id': 'quake_bridge', 'label': 'Packaged Quake frame/input probe',
                                   'status': 'passed' if probe.get('result') == 'pass' else 'failed',
                                   'duration': probe.get('seconds'), 'reason': probe.get('summary', '')})
    value['counts'] = {status: sum(s['status'] == status for s in value['steps'])
                       for status in ('pending', 'running', 'passed', 'failed', 'skipped')}
    return value


def safe_run(state, name):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,127}', name):
        raise ValueError('Invalid run identifier')
    root = (state / 'runs').resolve()
    path = (root / name).resolve(strict=True)
    if path.parent != root or not path.is_dir():
        raise ValueError('Run escapes history directory')
    return path


def log_excerpt(state, name, step_id):
    run = safe_run(state, name)
    value = progress_for(run)
    step = next((s for s in (value or {}).get('steps', []) if s['id'] == step_id), None)
    if not step:
        raise ValueError('Unknown build step')
    filename = 'packaged-quake-bridge.log' if step_id == 'quake_bridge' else 'build.log'
    path = (run / filename).resolve(strict=True)
    if path.parent != run or not path.is_file():
        raise ValueError('Log escapes run directory')
    with path.open('rb') as stream:
        size = os.fstat(stream.fileno()).st_size
        end = min(size, step.get('log_end', size))
        begin = max(0, step.get('log_start', 0), end - 65536)
        stream.seek(begin)
        text = stream.read(max(0, end-begin)).decode(errors='replace')
    if begin > step.get('log_start', 0):
        text = text.partition('\n')[2]  # never expose a cut-off credential prefix
    text = re.sub(r'(?i)(authorization\s*:\s*(?:bearer|basic)\s+)\S+', r'\1[REDACTED]', text)
    text = re.sub(r'(?i)((?:token|password|secret|api[_-]?key)\s*[=:]\s*)[^\s,;]+', r'\1[REDACTED]', text)
    text = re.sub(r'(https?://)[^/\s:@]+:[^/\s@]+@', r'\1[REDACTED]@', text)
    text = re.sub(r'-----BEGIN [^-]*PRIVATE KEY-----.*?-----END [^-]*PRIVATE KEY-----', '[REDACTED PRIVATE KEY]', text, flags=re.S)
    text = re.sub(r'\bsk-[A-Za-z0-9_-]{12,}', '[REDACTED]', text)
    return {'text': text, 'truncated': begin > step.get('log_start', 0),
            'historical': value.get('historical', False),
            'note': 'Historical full-log tail; exact step boundaries were not recorded' if value.get('historical') else ''}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('action', choices=('init', 'start', 'pass', 'skip', 'exit'))
    parser.add_argument('--step')
    parser.add_argument('--pid', type=int)
    parser.add_argument('--code', type=int, default=0)
    parser.add_argument('--reason', default='')
    args = parser.parse_args()
    sys.exit(emit(args.run, args.action, args.step, args.code, args.reason, args.pid))
