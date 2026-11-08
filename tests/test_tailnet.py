"""Test the helper with a fake client; never change host VPN state."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

helper = str(Path('build/os999-tailnet').resolve())
with tempfile.TemporaryDirectory(prefix='os999-tailnet-test-') as directory:
    root = Path(directory)
    fixture = root / 'status.json'
    fixture.write_text(json.dumps({'BackendState': 'Running', 'Self': {'HostName': 'test'}, 'Peer': {
        'a': {'HostName': 'nas', 'Online': True, 'TailscaleIPs': ['100.64.0.2']},
        'b': {'HostName': 'offline', 'Online': False, 'TailscaleIPs': ['fd7a:115c:a1e0::2']},
        'c': {'HostName': '\x1b[31mBAD', 'Online': False, 'TailscaleIPs': ['-oProxyCommand=bad']}}}))
    (root / 'prefs.json').write_text('{"CorpDNS":true,"RouteAll":false,"ShieldsUp":false}')
    cli = root / 'tailscale'
    cli.write_text('#!/bin/sh\ncd "$(dirname "$0")" || exit 1\nprintf "%s\\n" "$*" >> calls\ncase "$1" in\nstatus) cat status.json;;\ndebug) cat prefs.json;;\n*) exit 0;;\nesac\n')
    cli.chmod(0o700)
    env = dict(os.environ, OS999_TAILSCALE_CLI=str(cli))
    def run(commands='', status=False):
        return subprocess.run([helper] + (['--status'] if status else []), input=commands,
                              text=True, capture_output=True, env=env, timeout=15)
    result = run(status=True)
    assert result.returncode == 0 and 'nas' in result.stdout and 'fd7a:' in result.stdout
    assert 'ProxyCommand' not in result.stdout and 'BAD' not in result.stdout
    result = run('settings\ndns off\nno\nroutes on\nyes\nshields on\nyes\ndns off\nyes\nconnect\nyes\ndisconnect\nyes\nlogin\nyes\nssh 2 ian\nssh 1 -oBad\nquit\n')
    assert result.returncode == 0 and 'Cancelled' in result.stdout and 'Device is offline' in result.stdout
    calls = (root / 'calls').read_text().splitlines()
    for call in ['set --accept-routes=true', 'set --shields-up=true', 'set --accept-dns=false',
                 'up --timeout=30s', 'down', 'login --timeout=2m']:
        assert calls.count(call) == 1, (call, calls)
    assert 'Use: ssh' in result.stdout
    for malformed in ['{', '[]', '{"BackendState":2}', '{"BackendState":"Running","Peer":[]}',
                      '{"BackendState":"Running",}', '{"BackendState":"Running"}x',
                      '{"BackendState":"Running","x":' + '[' * 40 + '0' + ']' * 40 + '}']:
        fixture.write_text(malformed)
        assert run(status=True).returncode == 1, malformed
    fixture.write_text(json.dumps({'BackendState': 'Running', 'Peer': {
        'x': {'HostName': '\x1b[31mBAD', 'Online': False, 'TailscaleIPs': ['100.64.0.3']}}}))
    sanitized = run(status=True)
    assert '\x1b[31mBAD' not in sanitized.stdout and '?[31mBAD' in sanitized.stdout
    fixture.write_text('{"BackendState":"NeedsLogin","Peer":null}')
    assert run(status=True).returncode == 0
    fixture.write_text('x' * 1048577)
    assert run(status=True).returncode == 1
    cli.write_text('#!/bin/sh\nexit 7\n')
    assert run(status=True).returncode == 1
    cli.write_text('#!/bin/sh\nexec sleep 20\n')
    assert run(status=True).returncode == 1
print('test_tailnet: discovery, settings, cancellation, offline SSH and malformed input passed')
