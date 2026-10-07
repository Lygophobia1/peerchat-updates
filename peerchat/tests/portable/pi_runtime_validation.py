"""Isolated manual Pi validation; no root/system changes or production data."""
import argparse, hashlib, json, os, pathlib, signal, socket, ssl, sqlite3
import subprocess, time, resource

ROOT = pathlib.Path("/opt/peerchat-test")
parser = argparse.ArgumentParser()
parser.add_argument("mode", choices=["prepare", "validate", "parity", "start", "stop", "status", "epoch"])
parser.add_argument("--fixtures", type=pathlib.Path)
parser.add_argument("--version", required=True)
parser.add_argument("--port", type=int, default=17443)
args = parser.parse_args()
version = args.version
assert version.startswith("4.0.2-dev.") and version[10:].isdigit(), "version"
config_dir = ROOT / "config" / version
data = ROOT / "data" / version
logs = ROOT / "logs" / version
exe = ROOT / "server" / version / "PeerChat.Server"
config = config_dir / "server-config.json"
pidfile = data / "server.pid"
os.umask(0o077)

def pin():
    der = ssl.PEM_cert_to_DER_cert((config_dir / "server.crt").read_text())
    return hashlib.sha256(der).hexdigest().upper()

def env():
    result = os.environ.copy()
    result.update(HOME=str(data), TMPDIR=str(data / "tmp"),
                  DOTNET_EnableDiagnostics="0")
    return result

def start():
    assert not pidfile.exists(), "existing PID record; inspect before replacing"
    with (logs / "console.log").open("ab") as output:
        proc = subprocess.Popen([str(exe), "--config", str(config)],
            stdin=subprocess.DEVNULL, stdout=output, stderr=subprocess.STDOUT,
            cwd=data, env=env(), start_new_session=True)
    pidfile.write_text(str(proc.pid))
    for _ in range(100):
        assert proc.poll() is None, "server exited; inspect isolated console log"
        try:
            with socket.create_connection(("127.0.0.1", args.port), timeout=1):
                return proc
        except OSError:
            time.sleep(.1)
    raise AssertionError("listener startup timeout")

def stop(proc=None, sig=signal.SIGTERM):
    pid = int(pidfile.read_text())
    command = pathlib.Path("/proc") / str(pid) / "cmdline"
    owned = command.read_bytes()
    assert str(exe).encode() in owned and str(config).encode() in owned, "PID does not own isolated test configuration"
    os.kill(pid, sig)
    if proc is not None:
        assert proc.wait(timeout=35) == 0, "unclean shutdown"
    else:
        for _ in range(350):
            if not command.exists(): break
            time.sleep(.1)
        else: raise AssertionError("shutdown timeout; no forced kill")
    pidfile.unlink()

def connect():
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    # Disposable test certificate is authenticated by exact SHA-256 pin, not CA.
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    tls = context.wrap_socket(socket.create_connection(("127.0.0.1", args.port), timeout=5),
                              server_hostname="localhost")
    assert hashlib.sha256(tls.getpeercert(binary_form=True)).hexdigest().upper() == pin(), "pin"
    return tls

def exact(tls, n):
    data = b""
    while len(data) < n:
        part = tls.recv(n - len(data))
        assert part, "unexpected EOF"
        data += part
    return data

def exchange(tls, request):
    wire = json.dumps(request).encode()
    tls.sendall(len(wire).to_bytes(4, "big") + wire)
    size = int.from_bytes(exact(tls, 4), "big")
    assert 0 < size <= 100000, "frame bound"
    return json.loads(exact(tls, size))

def rpc(request):
    with connect() as tls:
        return exchange(tls, dict(version=1, **request))

if args.mode == "prepare":
    assert exe.exists() and not config_dir.exists(), "new isolated version required"
    for directory in [config_dir, data / "tmp", logs]:
        directory.mkdir(parents=True, mode=0o700)
    password = os.urandom(24).hex()
    (config_dir / "pfx-password").write_text(password)
    (config_dir / "turn-test-secret").write_text("PeerChat-integration-test-TURN-secret-2026")
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
        "-keyout", str(config_dir / "test-server.key"), "-out", str(config_dir / "server.crt"),
        "-days", "14", "-subj", "/CN=ENDER3PROPI",
        "-addext", "subjectAltName=DNS:localhost,DNS:ENDER3PROPI,IP:192.168.1.6"],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["openssl", "pkcs12", "-export", "-out", str(config_dir / "server.pfx"),
        "-inkey", str(config_dir / "test-server.key"), "-in", str(config_dir / "server.crt"),
        "-passout", "file:" + str(config_dir / "pfx-password")],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for path in config_dir.iterdir(): path.chmod(0o600)
    config.write_text(json.dumps(dict(DataDirectory=str(data),
        DatabasePath=str(data / "server.db"), LogDirectory=str(logs),
        Port=args.port, CertificatePath=str(config_dir / "server.pfx"),
        CertificatePasswordFile=str(config_dir / "pfx-password"),
        CertificateThumbprint="", ManagementPipe="", PublicHostname="192.168.1.6",
        TurnEnabled=True, TurnPublicHost="turn.test.invalid", TurnUdpPort=3478,
        TurnRealm="peerchat-test", TurnCredentialTtlSeconds=600,
        TurnRestSharedSecretFile=str(config_dir / "turn-test-secret"), FcmEnabled=False)))
    print("Prepared isolated development configuration; certificate SHA256=" + pin())
elif args.mode == "validate":
    epochs = []
    identity = "test-only-pi-lifecycle-device-0123456789"
    for index, sig in enumerate([signal.SIGINT, signal.SIGTERM, signal.SIGTERM]):
        proc = start()
        credentials = dict(username="pi-lifecycle", password="disposable-test-password")
        if index == 0: credentials["publicIdentity"] = identity
        reply = rpc(dict(type="register" if index == 0 else "login", **credentials))
        assert reply.get("ok"), "production registration/login"
        token = reply["sessionToken"]
        channel = connect()
        ready = exchange(channel, dict(version=1, type="events", sessionToken=token,
            publicIdentity=identity, clientHeartbeats=True))
        assert ready.get("ok") and ready.get("serverEpoch"), "event authorization"
        epochs.append(ready["serverEpoch"])
        partial = connect()
        partial.sendall(b"\x00\x00")
        idle = socket.create_connection(("127.0.0.1", args.port))
        time.sleep(2)
        stop(proc, sig)
        channel.close(); partial.close(); idle.close()
        with sqlite3.connect(str(data / "server.db")) as db:
            assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
            assert db.execute("SELECT count(*) FROM accounts WHERE username='pi-lifecycle'").fetchone()[0] == 1
        print("PASS: signal=" + sig.name + " active event/TLS/partial read drained; DB reopened")
    assert len(set(epochs)) == 3, "restart reused epoch"
    print("PASS: three unique server epochs, persistent account, clean process exit")
elif args.mode == "parity":
    fixture = args.fixtures.resolve(strict=True)
    assert fixture.is_relative_to(ROOT / "data"), "isolated fixtures only"
    harness = ROOT / "server" / ("parity" + version.rsplit(".", 1)[1]) / "PeerChat.Server.PortabilityTests"
    peak_rss = peak_threads = peak_fd = 0
    began = time.monotonic()
    with (ROOT / "logs" / (fixture.name + "-run.log")).open("xb") as output:
        proc = subprocess.Popen([str(harness), str(fixture), str(fixture / "mailbox")],
            stdout=output, stderr=subprocess.STDOUT, cwd=fixture, env=env())
        while proc.poll() is None:
            try:
                status = (pathlib.Path("/proc") / str(proc.pid) / "status").read_text()
                fields = dict(line.split(":", 1) for line in status.splitlines())
                peak_rss = max(peak_rss, int(fields.get("VmRSS", "0").split()[0]))
                peak_threads = max(peak_threads, int(fields.get("Threads", "0")))
                peak_fd = max(peak_fd, len(list((pathlib.Path("/proc") / str(proc.pid) / "fd").iterdir())))
            except (FileNotFoundError, ProcessLookupError):
                pass
            time.sleep(.1)
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    print(json.dumps(dict(exit=proc.returncode, wall_seconds=round(time.monotonic()-began, 2),
        user_cpu_seconds=round(usage.ru_utime, 2), system_cpu_seconds=round(usage.ru_stime, 2),
        max_rss_kib=usage.ru_maxrss, sampled_peak_rss_kib=peak_rss,
        peak_threads=peak_threads, peak_fd=peak_fd)))
    assert proc.returncode == 0, "parity failed; inspect isolated log"
elif args.mode == "epoch":
    identity = "test-only-pi-status-device-0123456789"
    credentials = dict(username="pi-validation-status", password="disposable-status-test-password")
    reply = rpc(dict(type="register", publicIdentity=identity, **credentials))
    if not reply.get("ok"):
        assert reply.get("error") == "registration_failed", "unexpected test account failure"
        reply = rpc(dict(type="login", **credentials))
    assert reply.get("ok"), "status test authentication"
    with connect() as channel:
        ready = exchange(channel, dict(version=1, type="events", sessionToken=reply["sessionToken"],
            publicIdentity=identity, clientHeartbeats=True))
        assert ready.get("ok"), "status test event authorization"
        print("Server epoch=" + ready["serverEpoch"])
elif args.mode == "start":
    proc = start()
    print("Started PID=" + str(proc.pid) + " address=192.168.1.6:" + str(args.port))
    print("Certificate SHA256=" + pin())
elif args.mode == "stop":
    stop()
    print("Stopped isolated test server cleanly")
else:
    print("Certificate SHA256=" + pin())
    if pidfile.exists():
        pid = int(pidfile.read_text())
        status = pathlib.Path("/proc") / str(pid) / "status"
        print(status.read_text() if status.exists() else "PID no longer exists")
