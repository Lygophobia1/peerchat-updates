"""Renew the existing Pi TLS key, retaining a pinned public certificate anchor.

Run daily as the existing unprivileged server owner. Never changes device keys,
account data, listener configuration, or the server executable. A different
server key requires a separate, explicitly confirmed trust migration.
"""
import argparse
import datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import tempfile

ANCHOR_OID = '2.25.220970347019663391330263105371358931173'


def run(arguments):
    result = subprocess.run([str(x) for x in arguments], capture_output=True)
    if result.returncode:
        raise RuntimeError('Certificate operation failed: ' + str(arguments[0]))
    return result.stdout


def public_key(certificate):
    return run(['openssl', 'x509', '-in', certificate, '-pubkey', '-noout'])


def prepare(current, password, anchor, directory):
    """All private intermediates stay in a mode-0700 directory on the Pi."""
    key = directory / 'temporary-key.pem'
    secret = directory / 'temporary-password'
    secret.write_text(os.urandom(32).hex())
    old = directory / 'current.crt'
    old.write_bytes(run(['openssl', 'pkcs12', '-in', current, '-passin', 'file:' + str(password), '-clcerts', '-nokeys']))
    assert public_key(old) == public_key(anchor), 'Server key changed; manual trust migration required'
    run(['openssl', 'pkcs12', '-in', current, '-passin', 'file:' + str(password), '-nocerts', '-out', key, '-passout', 'file:' + str(secret)])
    der = run(['openssl', 'x509', '-in', anchor, '-outform', 'DER'])
    assert len(der) <= 16000
    certificate = directory / 'server.crt'
    try:
        run(['openssl', 'req', '-x509', '-new', '-key', key, '-passin', 'file:' + str(secret), '-out', certificate,
             '-days', '365', '-sha256', '-subj', '/CN=ENDER3PROPI',
             '-addext', 'subjectAltName=DNS:localhost,DNS:ENDER3PROPI,IP:192.168.1.6,IP:173.225.14.37',
             '-addext', 'extendedKeyUsage=serverAuth', '-addext', ANCHOR_OID + '=DER:' + der.hex()])
        run(['openssl', 'pkcs12', '-export', '-out', directory / 'server.pfx', '-inkey', key,
             '-passin', 'file:' + str(secret), '-in', certificate, '-passout', 'file:' + str(password)])
        assert public_key(certificate) == public_key(anchor)
        run(['openssl', 'verify', '-CAfile', certificate, certificate])
    finally:
        key.unlink(missing_ok=True)
        secret.unlink(missing_ok=True)


def active_config(root):
    found = []
    for pidfile in (root / 'data').glob('4.0.2-dev.*/server.pid'):
        version = pidfile.parent.name
        config = root / 'config' / version / 'server-config.json'
        try:
            command = (Path('/proc') / str(int(pidfile.read_text())) / 'cmdline').read_bytes().split(b'\0')
        except FileNotFoundError:
            continue
        expected = [str(root / 'server' / version / 'PeerChat.Server').encode(), b'--config', str(config).encode()]
        if command[:3] == expected and json.loads(config.read_text())['Port'] == 7443:
            found.append((version, config))
    assert len(found) == 1, 'Expected one owned active server on port 7443'
    return found[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path('/opt/peerchat-test'))
    parser.add_argument('--anchor', type=Path, required=True)
    parser.add_argument('--anchor-pin', required=True)
    parser.add_argument('--force', action='store_true')
    args = parser.parse_args()
    os.umask(0o077)
    root = args.root.resolve(strict=True)
    area = root / 'config' / 'tls-renewal-20261021'
    with (area / 'renewal.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        anchor = args.anchor.resolve(strict=True)
        assert anchor.is_relative_to(root)
        expected = args.anchor_pin.lower()
        assert len(expected) == 64 and hashlib.sha256(run(['openssl', 'x509', '-in', anchor, '-outform', 'DER'])).hexdigest() == expected
        version, config = active_config(root)
        settings = json.loads(config.read_text())
        current = Path(settings['CertificatePath'])
        password = Path(settings['CertificatePasswordFile'])
        assert not current.is_symlink() and current.resolve(strict=True).is_relative_to(root)
        assert password.resolve(strict=True).is_relative_to(root)
        pem = run(['openssl', 'pkcs12', '-in', current, '-passin', 'file:' + str(password), '-clcerts', '-nokeys'])
        with tempfile.TemporaryDirectory(prefix='renew-', dir=area) as scratch:
            scratch = Path(scratch)
            leaf = scratch / 'leaf.crt'
            leaf.write_bytes(pem)
            due = subprocess.run(['openssl', 'x509', '-in', str(leaf), '-checkend', str(30 * 86400), '-noout'], capture_output=True).returncode != 0
            if not due and not args.force:
                print('Certificate valid for more than 30 days; no restart needed.')
                return
            prepare(current, password, anchor, scratch)
            # Validate everything before stopping. Save old PFX/public cert for rollback.
            stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
            backup = area / ('before-' + stamp)
            backup.mkdir(mode=0o700)
            public = config.parent / 'server.crt'
            shutil.copy2(current, backup / 'server.pfx')
            shutil.copy2(public, backup / 'server.crt')
            helper = root / 'server' / 'pi_runtime_validation.py'

            def lifecycle(mode):
                run(['python3', helper, mode, '--version', version, '--port', '7443'])

            lifecycle('stop')
            try:
                os.replace(scratch / 'server.pfx', current)
                shutil.copy2(scratch / 'server.crt', public)
                lifecycle('start')
                context = ssl.create_default_context(cafile=str(public))
                with context.wrap_socket(socket.create_connection(('127.0.0.1', 7443), timeout=5), server_hostname='localhost') as tls:
                    fingerprint = hashlib.sha256(tls.getpeercert(binary_form=True)).hexdigest()
                    assert fingerprint == hashlib.sha256(run(['openssl', 'x509', '-in', public, '-outform', 'DER'])).hexdigest()
            except Exception:
                pidfile = root / 'data' / version / 'server.pid'
                if pidfile.exists():
                    # A failed start may leave a dead PID record. Never signal
                    # a replacement process or skip restoring the previous PFX.
                    pid = int(pidfile.read_text())
                    if (Path('/proc') / str(pid)).exists():
                        lifecycle('stop')
                    else:
                        pidfile.unlink()
                shutil.copy2(backup / 'server.pfx', current)
                shutil.copy2(backup / 'server.crt', public)
                lifecycle('start')
                raise
            print(json.dumps({'result': 'renewed', 'version': version, 'sha256': fingerprint,
                              'valid_days': 365, 'renew_before_days': 30, 'same_key': True}))


if __name__ == '__main__':
    main()
