#!/usr/bin/env python3
"""Exercise the real embedded CEF client over IPC, without focus or key injection.
No trust-store changes, certificate exceptions, browser test hooks or desktop UI.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import select
import sys
import re
import socket
import subprocess
import tempfile
import time
from test_network_tls import certificate, server

ROOT = Path(__file__).resolve().parents[1]

class Message(ctypes.LittleEndianStructure):
    _fields_ = [(name, ctypes.c_int32) for name in
               ('kind', 'id', 'x', 'y', 'a', 'b', 'modifiers', 'keycode', 'ch', 'uch')]
    _fields_ += [('text', ctypes.c_char * 1000)]

CREATE, DESTROY, NAVIGATE, SHUTDOWN = 1, 2, 15, 23
TITLE, ADDRESS, LOADING, CLOSED, READY, SECURITY, TLS = 100, 101, 102, 106, 110, 111, 112

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, default=ROOT/'build/browser/Release/OS999Browser.app/Contents/MacOS/OS999Browser')
    args = p.parse_args()
    binary = args.binary.resolve(strict=True)
    receipt = ROOT/'build/embedded-security-results.json'
    receipt.write_text(json.dumps({'status':'running','binary':str(binary)})+'\n')
    if sys.platform == 'darwin':
        identities = subprocess.check_output(['security', 'find-identity', '-v', '-p', 'codesigning'], text=True)
        match = re.search(r'"(Apple Development:[^"]+)"', identities)
        identity = match.group(1) if match else '-'
        subprocess.run([sys.executable, str(ROOT/'browser/scripts/sign.py'), str(binary.parents[2]),
                        '--identity', identity], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    rows = []
    for flag in ('--no-sandbox', '--ignore-certificate-errors', '--allow-insecure-localhost',
                 '--disable-web-security', '--disable-features=NetworkServiceSandbox',
                 '--remote-debugging-port=9222', '--os999-test', '--os999-exit-after=1'):
        r = subprocess.run([str(binary), flag], capture_output=True, timeout=20)
        assert r.returncode == 64 and b'unsupported launch option' in r.stderr, flag
        rows.append({'case': flag.split('=')[0], 'passed': True})
    with tempfile.TemporaryDirectory(prefix='os999-embedded-security-') as tmp:
        d = Path(tmp).resolve()
        root = certificate(d, 'root')
        certificate(d, 'valid', root)
        downloads = d/'Downloads'; downloads.mkdir()
        parent, child = socket.socketpair()
        command = [str(binary), '--os999-embed-fd='+str(child.fileno()),
                   '--os999-profile='+str(d/'profile'), '--os999-download-dir='+str(downloads)]
        log = ROOT/'build/embedded-security-runtime.log'
        buffer = bytearray()
        events = []
        def send(kind, id=0, url=''):
            m = Message(kind=kind, id=id, x=100, a=640, b=480, text=url.encode())
            parent.sendall(bytes(m))
        def wait(predicate, seconds=40):
            end = time.monotonic()+seconds
            while time.monotonic() < end:
                if not select.select([parent], [], [], min(1, end-time.monotonic()))[0]: continue
                chunk = parent.recv(65536)
                assert chunk, 'helper closed IPC prematurely'
                buffer.extend(chunk)
                while len(buffer) >= ctypes.sizeof(Message):
                    m = Message.from_buffer_copy(buffer[:ctypes.sizeof(Message)])
                    del buffer[:ctypes.sizeof(Message)]
                    if m.kind in (TLS, SECURITY):
                        events.append({'kind':m.kind, 'tab':m.id, 'a':m.a, 'b':m.b,
                                       'message':bytes(m.text).decode(errors='replace')})
                    if predicate(m): return m
            raise AssertionError('Timed out waiting for embedded IPC result')
        with log.open('wb') as stream:
            proc = subprocess.Popen(command, pass_fds=(child.fileno(),), stdout=stream,
                                    stderr=subprocess.STDOUT)
            child.close()
            try:
                wait(lambda m:m.kind == READY)
                def check(name, id, url, verified=False, text=''):
                    start=len(events)
                    send(CREATE,id,url)
                    result=wait(lambda m: m.kind == TLS and m.id == id and
                                (m.a == 1 if verified else m.b == 1 and text in bytes(m.text).decode()))
                    if not verified: assert not any(e['tab']==id and e['a']==1 for e in events[start:])
                    rows.append({'case':name,'passed':True,'tab':id,'verified':bool(result.a)})
                    print('PASS',name,flush=True)
                with server(d) as base:
                    check('untrusted certificate',1,base+'/ok',text='certificate')
                check('plaintext',2,'http://example.com',text='HTTPS')
                check('local file',3,'file:///etc/hosts',text='HTTPS')
                check('URL credentials',4,'https://user:password@example.com',text='HTTPS')
                check('untrusted data document',5,'data:text/html,untrusted',text='HTTPS')
                check('trusted HTTPS in another tab',6,'https://example.com',verified=True)
                # Reuse an existing tab: a denied navigation clears its prior trust.
                send(NAVIGATE,6,'http://example.com')
                wait(lambda m:m.kind==TLS and m.id==6 and m.b==1 and m.a==0)
                send(NAVIGATE,6,'https://example.com')
                wait(lambda m:m.kind==TLS and m.id==6 and m.a==1)
                rows.append({'case':'trust reset and recovery in existing tab','passed':True})
                for id in range(1,7): send(DESTROY,id)
                send(SHUTDOWN)
                assert proc.wait(timeout=20)==0, 'unclean helper shutdown'
                rows.append({'case':'multi-tab shutdown','passed':True})
            finally:
                parent.close()
                if proc.poll() is None:
                    proc.terminate()
                    try: proc.wait(timeout=10)
                    except subprocess.TimeoutExpired: proc.kill(); proc.wait()
        (ROOT/'build/embedded-security-results.json').write_text(json.dumps(
            {'status':'passed','cases':rows,'events':events,'limits':'IPC/runtime checks; no desktop visual, keyboard, media or hardware acceptance.'},indent=2)+'\n')
    print(f'{len(rows)} embedded security checks passed')

if __name__ == '__main__': main()
