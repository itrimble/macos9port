#!/usr/bin/env python3
"""Local-only, explicit-ready integration queue. Never touches producer trees."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def git(repo, *args):
    return subprocess.check_output(
        ['git', '-C', str(repo), *args], stderr=subprocess.STDOUT, text=True).strip()


def write_json(path, value):
    temp = path.with_suffix('.tmp')
    temp.write_text(json.dumps(value, indent=2) + '\n')
    temp.replace(path)


def fingerprint(repo):
    """Include bytes, paths and modes, not just names of untracked files."""
    digest = hashlib.sha256()
    digest.update(subprocess.check_output(['git', '-C', str(repo), 'diff', 'HEAD', '--binary']))
    names = subprocess.check_output([
        'git', '-C', str(repo), 'ls-files', '--others', '--exclude-standard', '-z'])
    for raw in sorted(filter(None, names.split(b'\0'))):
        path = repo / os.fsdecode(raw)
        digest.update(raw + b'\0')
        digest.update(str(path.lstat().st_mode).encode() + b'\0')
        if path.is_symlink():
            digest.update(os.fsencode(os.readlink(path)))
        elif path.is_file():
            with path.open('rb') as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b''):
                    digest.update(block)
        else:
            raise ValueError(f'Unsupported snapshot entry: {path}')
    return digest.hexdigest()


def notify(message):
    # No shell interpolation, no UI input, and no notification dependencies.
    if sys.platform == 'darwin':
        subprocess.run(['osascript', '-e',
                        'on run argv\ndisplay notification (item 1 of argv) '
                        'with title "OS999 integration"\nend run', message],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def ready(repo, state, revision, dependencies, title='OS999 feature integration'):
    # Submitters and the worker must serialize queue writes and idempotency checks.
    with (state / 'lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        return ready_locked(repo, state, revision, dependencies, title)


def ready_locked(repo, state, revision, dependencies, title):
    commit = git(repo, 'rev-parse', '--verify', revision + '^{commit}')
    deps = [git(repo, 'rev-parse', '--verify', dep + '^{commit}') for dep in dependencies]
    path = state / 'queue' / (commit + '.json')
    if path.exists():
        return json.loads(path.read_text())
    item = {'commit': commit, 'dependencies': deps, 'status': 'ready', 'title': title[:160],
            'submitted': time.time(), 'attempts': 0}
    # Retain the exact source even if its producer later rebases its branch.
    git(repo, 'update-ref', 'refs/os999/ready/' + commit, commit)
    write_json(path, item)
    return item


def main_clean(repo, expected):
    return (git(repo, 'symbolic-ref', '--short', 'HEAD') == 'main'
            and git(repo, 'rev-parse', 'HEAD') == expected
            and not git(repo, 'status', '--porcelain', '--untracked-files=normal'))


def process(repo, state, gate):
    if (state / 'paused').exists():
        return
    with (state / 'lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return
        for path in sorted((state / 'queue').glob('*.json')):
            item = json.loads(path.read_text())
            if item['status'] in ('merged', 'failed'):
                continue
            if item.get('retry_after', 0) > time.time():
                continue
            # A crash leaves its candidate intact; restart with a fresh attempt.
            base = git(repo, 'rev-parse', 'main')
            if not main_clean(repo, base):
                write_json(state / 'status.json', {'status': 'waiting',
                           'reason': 'main checkout is dirty, moved, or not checked out'})
                return
            merged = {json.loads(p.read_text())['commit'] for p in
                      (state / 'queue').glob('*.json')
                      if json.loads(p.read_text())['status'] == 'merged'}
            if any(dep not in merged and subprocess.run(
                    ['git', '-C', str(repo), 'merge-base', '--is-ancestor', dep, base],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
                   for dep in item['dependencies']):
                continue
            item['attempts'] += 1
            # Serialized under the worker lock; numbers survive retries/restarts.
            counter = state / 'build-number.json'
            number = json.loads(counter.read_text())['last'] + 1 if counter.exists() else 1
            write_json(counter, {'last': number})
            item['number'] = number
            run = state / 'runs' / (item['commit'][:12] + '-' + str(item['attempts']))
            run.mkdir(parents=True, exist_ok=False)
            candidate = run / 'source'
            item.update(status='running', base=base, run=str(run), started=time.time())
            write_json(path, item)
            write_json(state / 'status.json', item)
            try:
                git(repo, 'worktree', 'add', '--detach', str(candidate), base)
                with (run / 'build.log').open('w') as log:
                    merge = subprocess.run(['git', '-C', str(candidate), 'merge',
                                            '--squash', item['commit']], stdout=log,
                                           stderr=subprocess.STDOUT)
                    if merge.returncode:
                        raise ValueError('Merge conflict; candidate retained for repair')
                    tree = git(candidate, 'write-tree')
                    if tree == git(repo, 'rev-parse', base + '^{tree}'):
                        item.update(status='merged', result=base, note='Already present')
                        write_json(path, item)
                        continue
                    env = dict(os.environ, BUILDFLEET_LOCAL='1', OS999_BUILD_NUMBER=str(number),
                               OS999_BUILD_TITLE=item['title'],
                               OS999_RECEIPT_DIR=str(run), OS999_SOURCE_TREE=tree)
                    checked = subprocess.run([str(gate), str(candidate)], env=env,
                                             stdout=log, stderr=subprocess.STDOUT)
                    if checked.returncode:
                        raise ValueError(f'Validation failed ({checked.returncode}); see build.log')
                if (git(candidate, 'write-tree') != tree or git(candidate, 'diff', '--name-only')
                        or git(candidate, 'ls-files', '--others', '--exclude-standard')):
                    raise ValueError('Validation changed source; refusing promotion')
                # Marker is produced only by the full gate, never by a launch alone.
                receipt = json.loads((run / 'receipt.json').read_text())
                if receipt.get('tree') != tree or receipt.get('result') != 'pass':
                    raise ValueError('Missing matching passing build receipt')
                artifact = Path(receipt['artifact'])
                if not artifact.is_dir():
                    raise ValueError('Passing artifact is missing')
                # Readable history: the build number and the submitted title, then the source commit.
                message = (f"Build {item['number']}: {item.get('title') or 'OS999 feature integration'}"
                           f"\n\nIntegrates ready commit {item['commit']}.")
                env = dict(os.environ, GIT_AUTHOR_NAME='Ian', GIT_AUTHOR_EMAIL='ian@example.com',
                           GIT_COMMITTER_NAME='Ian', GIT_COMMITTER_EMAIL='ian@example.com')
                result = subprocess.check_output(['git', '-C', str(candidate), 'commit-tree',
                         tree, '-p', base, '-m', message], env=env, text=True).strip()
                git(repo, 'update-ref', 'refs/os999/candidates/' + result, result)
                if not main_clean(repo, base):
                    raise ValueError('Main changed during validation; resubmit after reconciliation')
                # Git handles index locking and refuses overlapping local changes.
                git(repo, 'merge', '--ff-only', result)
                item.update(status='merged', result=result, completed=time.time())
                write_json(state / 'latest.json', dict(receipt, commit=result))
                notify('New complete build merged into local main: ' + result[:12])
            except (ValueError, OSError, subprocess.CalledProcessError) as error:
                item.update(status='failed', error=str(error), completed=time.time())
                notify('Integration needs repair; last passing build preserved')
            write_json(path, item)
            write_json(state / 'status.json', item)


def watch_cycle(repo, state, gate):
    """Expose observed readiness without manufacturing a build receipt."""
    status = {'pid': os.getpid(), 'last_poll': time.time(), 'repo': str(repo),
              'gate': str(gate), 'status': 'idle', 'reason': 'Waiting for explicit ready commits'}
    try:
        if (state / 'paused').exists():
            status.update(status='paused', reason='Integration paused')
        elif not main_clean(repo, git(repo, 'rev-parse', 'main')):
            status.update(status='waiting', reason='Main is dirty, moved, or not checked out')
        elif not gate.is_file() or not os.access(gate, os.X_OK):
            status.update(status='waiting', reason='Executable validation gate unavailable')
        elif not os.environ.get('OS999_QUAKE_REPOSITORY'):
            # Versions (CEF, Quake commit) come from each candidate's pins; only the repository location is local.
            status.update(status='waiting', reason='Quake engine repository build input is not configured')
        else:
            write_json(state / 'worker.json', status)
            process(repo, state, gate)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        status.update(status='error', reason=str(error))
    write_json(state / 'worker.json', status)
    return status


def cli():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path.home() / 'projects/macos9port')
    parser.add_argument('--state', type=Path, default=Path.home() / 'Library/Application Support/OS999/Integration')
    parser.add_argument('--gate', type=Path)
    sub = parser.add_subparsers(dest='command', required=True)
    submit = sub.add_parser('ready')
    submit.add_argument('commit')
    submit.add_argument('--depends', action='append', default=[])
    submit.add_argument('--title', default='OS999 feature integration')
    daemon = sub.add_parser('watch')
    daemon.add_argument('--interval', type=float, default=10)
    for command in ('once', 'status', 'pause', 'resume', 'fingerprint'):
        sub.add_parser(command)
    retry = sub.add_parser('retry')
    retry.add_argument('commit')
    args = parser.parse_args()
    repo, state = args.repo.resolve(), args.state.resolve()
    for child in ('queue', 'runs'):
        (state / child).mkdir(parents=True, exist_ok=True)
    gate = (args.gate or repo / 'scripts/integration-gate.sh').resolve()
    if args.command == 'ready':
        print(json.dumps(ready(repo, state, args.commit, args.depends, args.title), indent=2))
    elif args.command == 'once':
        process(repo, state, gate)
    elif args.command == 'watch':
        if args.interval < 1:
            parser.error('--interval must be at least one second')
        while True:
            watch_cycle(repo, state, gate)
            time.sleep(args.interval)
    elif args.command == 'pause':
        (state / 'paused').touch()
    elif args.command == 'resume':
        (state / 'paused').unlink(missing_ok=True)
    elif args.command == 'retry':
        commit = git(repo, 'rev-parse', '--verify', args.commit + '^{commit}')
        path = state / 'queue' / (commit + '.json')
        item = json.loads(path.read_text())
        if item['status'] != 'failed':
            raise ValueError('Only failed submissions may be retried')
        item.update(status='ready')
        write_json(path, item)
    elif args.command == 'fingerprint':
        print(fingerprint(repo))
    else:
        print(json.dumps({'paused': (state / 'paused').exists(), 'queue':
              [json.loads(p.read_text()) for p in sorted((state / 'queue').glob('*.json'))]}, indent=2))


if __name__ == '__main__':
    cli()
