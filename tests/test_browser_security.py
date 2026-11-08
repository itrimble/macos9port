#!/usr/bin/env python3
"""Execute the built Chromium helper; do not install certificates or disable TLS."""
from pathlib import Path
import subprocess
import os
import json
import signal
import tempfile
from test_network_tls import certificate, server

ROOT = Path(__file__).resolve().parents[1]
APP = ROOT/'build/browser/Release/OS999Browser.app'
BINARY = APP/'Contents/MacOS/OS999Browser'

def main():
    assert BINARY.is_file(), 'Build OS999Browser first'
    # Sign only this worktree's new artifact, never another live app bundle.
    subprocess.run(['python3', str(ROOT/'browser/scripts/sign.py'), str(APP)], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    for option in ('--no-sandbox', '--ignore-certificate-errors', '--allow-insecure-localhost',
                   '--disable-web-security', '--ignore-certificate-errors-spki-list=anything',
                   '--disable-features=NetworkServiceSandbox'):
        r = subprocess.run([str(BINARY), option], capture_output=True, timeout=10)
        assert r.returncode == 64 and b'unsupported launch option' in r.stderr, option
        print('PASS rejects '+option.split('=')[0], flush=True)
    with tempfile.TemporaryDirectory(prefix='os999-browser-tls-') as tmp:
        d = Path(tmp)
        root = certificate(d, 'root')
        certificate(d, 'valid', root)
        profiles = d/'profiles'
        profiles.mkdir()
        alias = d/'profile-alias'
        alias.symlink_to(profiles.resolve(), target_is_directory=True)
        results = []
        def run(name, url, expected, forbidden=None):
            command = [str(BINARY), '--os999-test', '--url='+url,
                       '--os999-test-profile='+str(alias/name), '--os999-exit-after=20']
            child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                     text=True, start_new_session=True)
            try:
                stdout, stderr = child.communicate(timeout=60)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                stdout, stderr = child.communicate()
                stderr += "\nBrowser test exceeded 60 seconds."
            r = subprocess.CompletedProcess(command, child.returncode, stdout, stderr)
            (ROOT/'build'/f'browser-security-{name}.log').write_text(stderr)
            # Persist redacted security events, not page console/cookie content.
            events = '\n'.join(line for line in r.stderr.splitlines() if 'OS999_SECURITY ' in line)
            cache_valid = 'cache_path is invalid' not in r.stderr
            passed = r.returncode == 0 and cache_valid and expected in events and (not forbidden or forbidden not in events)
            results.append({'case': name, 'passed': bool(passed), 'exit': r.returncode, 'cache_valid': cache_valid, 'events': events})
            print(f'{"PASS" if passed else "FAIL"} {name}: exit={r.returncode} {events}', flush=True)
        with server(d) as base:
            run('untrusted', base+'/ok', 'certificate-denied', 'verified=1')
        run('plaintext', 'http://example.com', 'navigation-denied', 'verified=1')
        run('public-trust', 'https://example.com', 'verified=1', 'certificate-denied')
        (ROOT/'build/browser-security-results.json').write_text(json.dumps(results, indent=2)+'\n')
        assert all(row['passed'] for row in results), 'Browser runtime gates failed; see build/browser-security-results.json'
    print('9 browser security cases passed; UI and machine-client-identity selection unverified')

if __name__ == '__main__':
    main()
