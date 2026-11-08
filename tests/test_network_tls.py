#!/usr/bin/env python3
"""Synthetic loopback TLS tests; no changes to machine trust."""
import contextlib
import datetime as dt
import http.server
import ipaddress
import os
from pathlib import Path
import socket
import ssl
import subprocess
import tempfile
import threading
import time
import warnings
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID
ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get('OS999_NETWORK_TEST_BINARY', ROOT / 'build/test_modern_network'))

def certificate(d, name, issuer=None, expired=False, host='localhost', client=False):
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, name)])
    now = dt.datetime.now(dt.timezone.utc)
    b = (x509.CertificateBuilder().subject_name(subject)
         .issuer_name(issuer[0].subject if issuer else subject).public_key(key.public_key())
         .serial_number(x509.random_serial_number()).not_valid_before(now-dt.timedelta(days=3))
         .not_valid_after(now+dt.timedelta(days=-1 if expired else 2))
         .add_extension(x509.BasicConstraints(ca=issuer is None, path_length=None), True))
    if issuer:
        names = [x509.DNSName(host)]
        if host == 'localhost':
            names += [x509.IPAddress(ipaddress.ip_address(ip)) for ip in ('127.0.0.1', '::1')]
        b = b.add_extension(x509.SubjectAlternativeName(names), False)
        b = b.add_extension(x509.ExtendedKeyUsage([
            ExtendedKeyUsageOID.CLIENT_AUTH if client else ExtendedKeyUsageOID.SERVER_AUTH]), False)
    cert = b.sign(issuer[1] if issuer else key, hashes.SHA256())
    (d/f'{name}.pem').write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    path = d/f'{name}.key'
    path.write_bytes(key.private_bytes(serialization.Encoding.PEM,
                    serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    path.chmod(0o600)
    return cert, key

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass
    def do_GET(self):
        if self.path == '/slow':
            time.sleep(2)
        redirects = {'/redirect': '/ok', '/downgrade': 'http://localhost/plain',
                     '/denied-hop': '/forbidden', '/cycle': '/cycle'}
        if self.path in redirects:
            self.send_response(302)
            self.send_header('Location', redirects[self.path])
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        self.send_response(404 if self.path == '/missing' else 206 if self.path == '/partial' else 200)
        body = b'x'*8192 if self.path == '/large' else b'secure fixture'
        if self.path == '/partial':
            self.send_header('Content-Range', f'bytes 0-{len(body)-1}/{len(body)+100}')
        if self.path != '/unknown-length':
            self.send_header('Content-Length', len(body)+(10 if self.path == '/truncated' else 0))
        self.end_headers()
        with contextlib.suppress(BrokenPipeError, ssl.SSLError, ConnectionResetError):
            self.wfile.write(body)

@contextlib.contextmanager
def server(d, name='valid', version=None, client=False, ipv6=False):
    class Server(http.server.ThreadingHTTPServer):
        address_family = socket.AF_INET6 if ipv6 else socket.AF_INET
        daemon_threads = True
        def handle_error(self, request, client_address):
            pass  # Expected TLS rejection/timeout closes the peer early.
    httpd = Server(('::1' if ipv6 else '127.0.0.1', 0), Handler)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    if version:
        with warnings.catch_warnings():
            warnings.simplefilter('ignore', DeprecationWarning)
            ctx.minimum_version = ctx.maximum_version = version
        if version == ssl.TLSVersion.TLSv1_1:
            ctx.set_ciphers('DEFAULT:@SECLEVEL=0')
    ctx.load_cert_chain(d/f'{name}.pem', d/f'{name}.key')
    if client:
        ctx.verify_mode = ssl.CERT_REQUIRED
        ctx.load_verify_locations(d/'root.pem')
    httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    try:
        yield f'https://{"[::1]" if ipv6 else "localhost"}:{httpd.server_port}'
    finally:
        httpd.shutdown()
        httpd.server_close()
        thread.join()

def main():
    count = 0
    with tempfile.TemporaryDirectory(prefix='os999-tls-') as tmp:
        d = Path(tmp)
        root = certificate(d, 'root')
        certificate(d, 'valid', root)
        certificate(d, 'expired', root, expired=True)
        certificate(d, 'wrong', root, host='wrong.invalid')
        certificate(d, 'client', root, client=True)
        other_root = certificate(d, 'other-root')
        certificate(d, 'untrusted-client', other_root, client=True)
        dest = d/'download'
        (d/'.curlrc').write_text('insecure\nproxy = http://127.0.0.1:1\n')
        def check(name, url, expected, ca=None, limit=1024, timeout=3000, deny='-', cancel='-', client=False):
            nonlocal count
            dest.write_bytes(b'previous file')
            args = [str(BINARY), url, str(dest), str(ca or d/'root.pem'), str(limit), str(timeout), deny, cancel]
            if client:
                identity = 'client' if client is True else client
                args += [str(d/f'{identity}.pem'), str(d/f'{identity}.key')]
            env = dict(os.environ, CURL_HOME=str(d), CURL_CA_BUNDLE=str(d/'root.pem'), HTTPS_PROXY='http://127.0.0.1:1', ALL_PROXY='http://127.0.0.1:1')
            r = subprocess.run(args, capture_output=True, text=True, timeout=10, env=env)
            statuses = expected if isinstance(expected, tuple) else (expected,)
            assert r.returncode in statuses, (name, r.returncode, r.stdout, r.stderr)
            assert dest.read_bytes() == (b'secure fixture' if r.returncode == 0 else b'previous file'), name
            assert not list(d.glob('download.net-*')), name
            print(f'PASS {name}: {r.stdout.strip()}', flush=True)
            count += 1
        with server(d) as base:
            check('valid chain and DNS', base+'/ok', 0)
            check('IPv4 identity', base.replace('localhost', '127.0.0.1')+'/ok', 0)
            check('untrusted root', base+'/ok', 2, ca='-')
            check('missing CA file', base+'/ok', 2, ca=d/'absent.pem')
            check('relative HTTPS redirect', base+'/redirect', 0)
            check('HTTP downgrade', base+'/downgrade', 1)
            check('redirect permission', base+'/denied-hop', 1, deny='/forbidden')
            check('redirect cycle', base+'/cycle', 1)
            check('initial permission', base+'/forbidden', 1, deny='/forbidden')
            check('size limit', base+'/large', 5)
            check('unknown length limit', base+'/unknown-length', 5, limit=5)
            check('HTTP error', base+'/missing', 6)
            check('partial body', base+'/truncated', 8)
            check('unsolicited partial response', base+'/partial', 6)
            check('deadline', base+'/slow', 3, timeout=100)
            check('cancelled while active', base+'/slow', 4, cancel='later')
            check('cancelled before connection', base+'/ok', 4, cancel='cancel')
            check('cancelled during final flush', base+'/ok', 4, cancel='flush')
            check('URL credentials', base.replace('https://', 'https://secret:password@')+'/ok', 1)
        for name in ('expired', 'wrong'):
            with server(d, name) as base:
                check(name+' certificate', base+'/ok', 2)
        for version in (ssl.TLSVersion.TLSv1_2, ssl.TLSVersion.TLSv1_3):
            with server(d, version=version) as base:
                check(version.name, base+'/ok', 0)
        with server(d, version=ssl.TLSVersion.TLSv1_1) as base:
            check('TLS 1.1 rejected', base+'/ok', 2)
        with server(d, client=True) as base:
            check('client certificate', base+'/ok', 0, client=True)
            check('client identity redirect denied', base+'/redirect', 1, client=True)
            # TLS 1.3 client-auth alerts can surface during send/receive rather
            # than the handshake: accept NET_TLS or NET_TRANSPORT, never success.
            check('missing client identity rejected', base+'/ok', (2, 8))
            check('untrusted client identity rejected', base+'/ok', (2, 8), client='untrusted-client')
        with server(d, ipv6=True) as base:
            check('IPv6 identity', base+'/ok', 0)
    print(f'{count} TLS integration cases passed')

if __name__ == '__main__':
    main()
