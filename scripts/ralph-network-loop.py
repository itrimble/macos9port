#!/usr/bin/env python3
"""Bounded Ralph verification gate. The owning agent repairs failed iterations.
Never treats a retry as a fix. Failed commands stop immediately with evidence.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import signal
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def snapshot():
    names = subprocess.check_output(['git', 'ls-files', '-z', '-c', '-o', '--exclude-standard'], cwd=ROOT)
    return {name: digest(ROOT/name) for name in sorted(set(names.decode().split('\0')))
            if name and (ROOT/name).is_file() and not (ROOT/name).is_symlink()}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--passes', type=int, choices=(1, 2, 3), default=2)
    p.add_argument('--browser', action='store_true', help='require configured CEF build and browser security tests')
    args = p.parse_args()
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    output = ROOT/'build/ralph-network'/stamp
    output.mkdir(parents=True)
    source = snapshot()
    (output/'source-manifest.json').write_text(json.dumps(source, indent=2)+'\n')
    report = {'host': platform.node(), 'platform': platform.platform(), 'utc': stamp,
              'base': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'source_sha256': digest(output/'source-manifest.json'), 'browser_required': args.browser, 'passes': [], 'status': 'running',
              'limits': 'Host tests only; no guest TCP API, Linux, revocation, UI or human acceptance claim.'}
    commands = [['make', '-j2', 'all', 'test'], ['make', 'test-network']]
    if args.browser:
        commands += [['cmake', '--build', 'build/browser', '--target', 'OS999Browser', '--parallel', '2'],
                     ['python3', 'tests/test_browser_security.py']]
    env = dict(os.environ, BUILDFLEET_LOCAL='1')  # Explicit project CLAUDE.md override.
    for iteration in range(1, args.passes+1):
        for index, command in enumerate(commands):
            log = output/f'{iteration}-{index}.log'
            with log.open('wb') as stream:
                child = subprocess.Popen(command, cwd=ROOT, env=env, stdout=stream,
                                         stderr=subprocess.STDOUT, start_new_session=True)
                try:
                    rc = child.wait(timeout=900)
                except subprocess.TimeoutExpired:
                    os.killpg(child.pid, signal.SIGKILL)
                    child.wait()
                    rc = 'timeout'
            row = {'iteration': iteration, 'command': command, 'exit': rc, 'log': log.name,
                   'log_sha256': digest(log)}
            report['passes'].append(row)
            print(f'iteration {iteration}: {command[0]} exit={rc}: {log}', flush=True)
            if rc != 0 or snapshot() != source:
                report['status'] = 'failed' if rc != 0 else 'source-changed'
                (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
                return 1
    report['status'] = 'passed'
    report['artifacts'] = {str(p.relative_to(ROOT)): digest(p) for p in (
        ROOT/'build/ospi', ROOT/'build/test_modern_network',
        ROOT/'build/browser/Release/OS999Browser.app/Contents/MacOS/OS999Browser') if p.is_file()}
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'Ralph verification complete: {output}/report.json')
    return 0

if __name__ == '__main__':
    sys.exit(main())
