"""Read project evidence without treating source presence as acceptance."""
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import time
from build_progress import progress_for, safe_run

GROUPS = {'C': 'Provenance', 'S': 'Startup', 'L': 'Storage', 'R': 'Toolbox & runtime',
          'V': 'Finder & appearance', 'G': 'Graphics & games', 'B': 'Browser',
          'Q': 'QuickTime', 'N': 'Networking', 'A': 'Release & platforms'}
STATES = ('verified', 'implemented', 'in_progress', 'blocked', 'pending')


def load_json(path, default):
    try:
        return json.loads(path.read_text())
    except FileNotFoundError:
        return default


def git(repo, *args):
    try:
        return subprocess.check_output(['git', '-C', str(repo), *args],
                                       text=True, stderr=subprocess.DEVNULL, timeout=8).strip()
    except (subprocess.SubprocessError, OSError):
        return ''


def checklist(repo, configuration):
    tasks = []
    for relative in configuration.get('checklists', ['docs/DEVELOPMENT-CHECKLIST.md']):
        path = (repo / relative).resolve()
        if not path.is_relative_to(repo.resolve()):
            raise ValueError('Checklist path escapes repository')
        lines = path.read_text().splitlines()
        current = None
        for number, line in enumerate(lines, 1):
            match = re.match(r'^- \[([ xX])\] ([A-Z]\d+)\s+(.*)', line)
            if match:
                code, title = match[2], match[3]
                override = configuration.get('task_evidence', {}).get(code, {})
                state = 'verified' if match[1].lower() == 'x' else override.get('status', 'pending')
                if state not in STATES or (state == 'verified' and match[1].lower() != 'x'):
                    state = 'pending'
                current = {'id': code, 'title': title, 'detail': title,
                           'component': GROUPS.get(code[0], 'Other'), 'status': state,
                           'source': relative, 'line': number, 'evidence': override.get('evidence', ''),
                           'basis': 'Checklist acceptance' if state == 'verified' else 'Open acceptance gate'}
                tasks.append(current)
            elif current and line.startswith('  '):
                current['detail'] += '\n' + line.strip()
            else:
                current = None
    for task in configuration.get('parity_tasks', []):
        item = dict(task)
        if item.get('status') not in STATES:
            item['status'] = 'pending'
        if item['status'] == 'verified' and not item.get('evidence'):
            item['status'] = 'pending'
        item.setdefault('source', 'config/dashboard.json')
        item.setdefault('detail', item['title'])
        item.setdefault('basis', 'Parity catalog; acceptance evidence required')
        tasks.append(item)
    ids = [task['id'] for task in tasks]
    if len(ids) != len(set(ids)):
        raise ValueError('Duplicate task IDs in dashboard catalog')
    return tasks


def source_text(repo, relative, allowed):
    if relative not in allowed:
        raise ValueError('Source is not in the dashboard evidence catalog')
    path = (repo / relative).resolve()
    if not path.is_relative_to(repo.resolve()) or not path.is_file():
        raise ValueError('Source is unavailable')
    if path.stat().st_size > 2_000_000:
        raise ValueError('Source exceeds display limit')
    return path.read_text(errors='replace')


def collect(repo, state):
    configuration = load_json(repo / 'config/dashboard.json', {})
    if not isinstance(configuration, dict):
        raise ValueError("Dashboard configuration must be an object")
    tasks = checklist(repo, configuration)
    components = []
    for name in dict.fromkeys(task['component'] for task in tasks):
        counts = {status: sum(t['component'] == name and t['status'] == status for t in tasks)
                  for status in STATES}
        components.append({'name': name, 'counts': counts, 'total': sum(counts.values())})
    counts = {status: sum(t['status'] == status for t in tasks) for status in STATES}
    builds, alerts = [], []
    for path in sorted((state / 'queue').glob('*.json')):
        try:
            builds.append(load_json(path, {}))
        except (ValueError, OSError):
            alerts.append({'level': 'warning', 'title': 'Unreadable queue entry', 'detail': path.name})
    # Manual validations are observable history, never actionable queue items.
    queued_numbers = {item.get('number') for item in builds}
    for path in sorted((state / 'runs').glob('*/build-state.json')):
        try:
            if path.parent.resolve().parent != (state / 'runs').resolve():
                continue
            item = load_json(path, {})
            if item.get('kind') != 'manual_snapshot_validation' or type(item.get('number')) is not int:
                continue
            if item['number'] in queued_numbers:
                continue  # promotion/failure recorded by the queue takes precedence
            item.setdefault('commit', '')
            builds.append(item)
        except (ValueError, OSError, AttributeError):
            alerts.append({'level': 'warning', 'title': 'Unreadable manual build history', 'detail': path.parent.name})
    builds.sort(key=lambda item: item.get('number', 0), reverse=True)
    for item in builds:
        if item.get('run'):
            try:
                run = safe_run(state, Path(item['run']).name)
                item['run_id'] = run.name
                item['progress'] = progress_for(run, item.get('source_tree'))
                if (item['progress'] or {}).get('status') == 'failed' and item.get('status') != 'merged':
                    item['recorded_status'] = item.get('status')
                    item['status'] = 'failed'
                    item['error'] = item['progress'].get('reason', 'Build stage failed')
            except (ValueError, OSError, KeyError, TypeError) as error:
                item['progress_error'] = str(error)
        if item.get('status') == 'failed':
            alerts.append({'level': 'error', 'title': item.get('title', 'Integration failed'),
                           'detail': item.get('error', 'Inspect the retained build log')})
    status = git(repo, 'status', '--porcelain', '--untracked-files=normal')
    worktrees = []
    for block in git(repo, 'worktree', 'list', '--porcelain').split('\n\n'):
        values = dict(line.split(' ', 1) for line in block.splitlines() if ' ' in line)
        if 'worktree' in values:
            worktrees.append({'name': Path(values['worktree']).name, 'path': values['worktree'],
                              'head': values.get('HEAD', '')[:12],
                              'branch': values.get('branch', 'detached').removeprefix('refs/heads/')})
    milestones = []
    by_id = {task['id']: task for task in tasks}
    for entry in configuration.get('milestones', []):
        item = dict(entry)
        members = [by_id[code] for code in item.get('tasks', []) if code in by_id]
        item['accepted'] = sum(task['status'] == 'verified' for task in members)
        item['total'] = len(item.get('tasks', []))
        item['percent'] = round(100 * item['accepted'] / item['total']) if item['total'] else 0
        milestones.append(item)
    metrics = configuration.get('metrics', [])
    for metric in metrics:
        # Missing provenance invalidates a numeric comparison.
        for side in ('baseline', 'os999'):
            measurement = metric.get(side)
            if measurement is not None and (not isinstance(measurement, dict)
                    or not all(measurement.get(k) for k in ('date', 'source', 'hardware'))
                    or type(measurement.get('value')) not in (int, float)
                    or not math.isfinite(measurement['value']) or measurement['value'] < 0):
                metric[side] = None
    if not any(metric.get('baseline') for metric in metrics):
        alerts.append({'level': 'info', 'title': 'Mac OS 9.2 baseline not recorded',
                       'detail': 'Performance comparisons stay empty until equivalent measurements are supplied.'})
    if not configuration.get('catalog_approved', False):
        alerts.append({'level': 'warning', 'title': 'Parity catalog needs scope review',
                       'detail': 'Percentages describe tracked acceptance gates, not a certification of complete Mac OS 9.2 parity.'})
    allowed = {task['source'] for task in tasks} | set(configuration.get('sources', []))
    result = {'project': 'OS999', 'counts': counts, 'total': len(tasks), 'tasks': tasks,
              'components': components, 'milestones': milestones, 'builds': builds,
              'alerts': alerts, 'apps': configuration.get('apps', []), 'metrics': metrics,
              'anatomy': configuration.get('anatomy', []), 'dependencies': configuration.get('dependencies', []),
              'git': {'branch': git(repo, 'branch', '--show-current'), 'head': git(repo, 'rev-parse', 'HEAD'),
                      'main': git(repo, 'rev-parse', 'main'), 'changed': len(status.splitlines()),
                      'worktrees': worktrees}, 'watcher_paused': (state / 'paused').exists(),
              'watcher': load_json(state / 'worker.json', None),
              'latest': load_json(state / 'latest.json', None), 'sources': sorted(allowed),
              'catalog_version': configuration.get('catalog_version', 1),
              'snapshot_time': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}
    stable = dict(result)
    stable.pop('snapshot_time')
    result['revision'] = hashlib.sha256(json.dumps(stable, sort_keys=True).encode()).hexdigest()[:16]
    return result
