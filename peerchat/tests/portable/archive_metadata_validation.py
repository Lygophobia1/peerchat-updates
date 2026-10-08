"""Signed archive metadata RPC regression on an ISOLATED Pi test DB only.

Never runs against the physical-client DB or port 7443. Normal device identities,
private keys, credentials and message/history data are not used.
"""
import argparse
import base64
import hashlib
import json
import pathlib
import secrets
import socket
import sqlite3
import ssl
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument("--version", required=True)
p.add_argument("--fixtures", type=pathlib.Path, required=True)
p.add_argument("--port", type=int, default=17443)
args = p.parse_args()
assert args.version.startswith("4.0.2-dev.") and args.version[10:].isdigit()
assert args.port != 7443, "Never test against the physical-client listener"
root = pathlib.Path("/opt/peerchat-test")
data = root / "data" / args.version
config_dir = root / "config" / args.version
config = json.loads((config_dir / "server-config.json").read_text())
db_path = data / "server.db"
assert config["DatabasePath"] == str(db_path) and config["DataDirectory"] == str(data)
assert config["Port"] == args.port
fixture = args.fixtures.resolve(strict=True)
assert fixture.is_relative_to(root / "data") and fixture != data
tool = root / "server" / "pi_runtime_validation.py"
token = secrets.token_hex(32)
token_hash = hashlib.sha256(token.encode()).hexdigest()
proof = lambda name: (fixture / (name + ".bin")).read_bytes()
owner = hashlib.sha256(proof("issuer-public")).hexdigest()

def descriptor_device(name):
    wire = proof(name)
    length = int.from_bytes(wire[4:12], "little")
    payload = wire[12:12+length]
    offset = 4
    domain_length = int.from_bytes(payload[offset:offset+8], "little")
    offset += 8 + domain_length + 8 + 8
    size = int.from_bytes(payload[offset:offset+8], "little")
    assert size == 64
    return payload[offset+8:offset+8+size].decode("ascii")

reader = descriptor_device("reader-descriptor")
other = descriptor_device("other-descriptor")
assert descriptor_device("issuer-descriptor") == owner
assert not (data / "server.pid").exists(), "isolated instance must be stopped for seeding"
resuming = False
with sqlite3.connect(db_path) as db:
    existing = db.execute("SELECT id,username FROM accounts WHERE id IN (8,9) ORDER BY id").fetchall()
    if not existing:
        db.executescript("INSERT INTO accounts(id,username,password_salt,password_hash,created_utc) VALUES(8,'archive-rpc-test',X'00',X'00',1),(9,'archive-other',X'00',X'00',1);")
        for device in (owner, reader, other):
            db.execute("INSERT INTO devices(account_id,public_identity,created_utc) VALUES(8,?,1)", (device,))
    else:
        # Resume only the exact pre-revocation fixture. Never reset/delete a
        # populated archive DB, even inside this test harness.
        assert existing == [(8, 'archive-rpc-test'), (9, 'archive-other')]
        assert set(row[0] for row in db.execute("SELECT public_identity FROM devices WHERE account_id=8")) == {owner, reader, other}
        if db.execute("SELECT count(*) FROM sqlite_master WHERE name='archive_device_grants'").fetchone()[0]:
            rows = db.execute("SELECT proof FROM archive_device_grants ORDER BY device").fetchall()
            assert not rows or sorted(row[0] for row in rows) == sorted((proof("serve"), proof("read")))
            resuming = bool(rows)
    db.execute("INSERT INTO sessions(token_hash,account_id,expires_utc) VALUES(?,8,?)", (token_hash, int(time.time()) + 600))

pin = hashlib.sha256(ssl.PEM_cert_to_DER_cert((config_dir / "server.crt").read_text())).hexdigest()
def lifecycle(mode):
    subprocess.run(["python3", str(tool), mode, "--version", args.version, "--port", str(args.port)], check=True)

def exact(tls, n):
    result = b""
    while len(result) < n:
        part = tls.recv(n-len(result))
        if not part:
            raise EOFError("TLS peer rejected/closed before a complete response")
        result += part
    return result

def rpc(kind, device=owner, name=None, auth=True):
    request = dict(version=1, type=kind, publicIdentity=device)
    if auth:
        request["sessionToken"] = token
    if name:
        request["proof"] = base64.b64encode(proof(name)).decode("ascii")
    # Disposable certificate: authenticate exact SHA-256, not an insecure
    # production trust setting.
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    with context.wrap_socket(socket.create_connection(("127.0.0.1", args.port), timeout=5), server_hostname="localhost") as tls:
        assert hashlib.sha256(tls.getpeercert(binary_form=True)).hexdigest() == pin
        wire = json.dumps(request).encode()
        tls.sendall(len(wire).to_bytes(4, "big") + wire)
        try:
            size = int.from_bytes(exact(tls, 4), "big")
        except EOFError:
            if not auth:
                return {"ok": False}
            raise
        assert 0 < size <= 100000
        return json.loads(exact(tls, size))

def accepted(kind, name, device=owner):
    assert rpc(kind, device=device, name=name).get("ok"), "signed metadata admission"

def state():
    result = rpc("archive_authorization_state", device=reader)
    assert result.get("ok")
    return result

def open_tls():
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    tls = context.wrap_socket(socket.create_connection(("127.0.0.1", args.port), timeout=5), server_hostname="localhost")
    assert hashlib.sha256(tls.getpeercert(binary_form=True)).hexdigest() == pin
    return tls

def write_frame(tls, wire):
    assert len(wire) <= 100000
    tls.sendall(len(wire).to_bytes(4, "big") + wire)

def read_frame(tls):
    size = int.from_bytes(exact(tls, 4), "big")
    assert 0 < size <= 100000
    return exact(tls, size)

def grant_scope(name):
    wire = proof(name)
    payload = wire[12:12+int.from_bytes(wire[4:12], "little")]
    offset = 4
    def number():
        nonlocal offset
        value = int.from_bytes(payload[offset:offset+8], "little")
        offset += 8
        return value
    def field():
        nonlocal offset
        size = number()
        value = payload[offset:offset+size]
        offset += size
        return value
    assert field() == b"PeerChat/ArchiveAuthorization/v1" and number() == 1
    assert number() == 8
    assert field().decode() == owner
    field(); field(); number()
    generation = number()
    number()
    issuance = field().decode()
    return generation, issuance

def relay_request(device, target, read_name="read"):
    source_generation, source_id = grant_scope("serve")
    reader_generation, reader_id = grant_scope(read_name)
    return dict(version=1, type="relay_attach", sessionToken=token, publicIdentity=device,
        targetAccountId="8", targetPublicIdentity=target, purpose="archive_history",
        archiveIssuer=owner, archiveSource=owner, archiveReader=reader,
        archiveSourceGeneration=str(source_generation), archiveReaderGeneration=str(reader_generation),
        archiveSourceGrantId=source_id, archiveReaderGrantId=reader_id)

def reject_relay(request):
    with open_tls() as tls:
        write_frame(tls, json.dumps(request).encode())
        assert not json.loads(read_frame(tls)).get("ok")

def pair_relay(read_name="read"):
    left, right = open_tls(), open_tls()
    try:
        write_frame(left, json.dumps(relay_request(owner, reader, read_name)).encode())
        write_frame(right, json.dumps(relay_request(reader, owner, read_name)).encode())
        l, r = json.loads(read_frame(left)), json.loads(read_frame(right))
        assert l.get("ok") and r.get("ok"), "relay admission failed: " + str(l.get("error")) + " / " + str(r.get("error"))
        assert l["purpose"] == r["purpose"] == "archive_history"
        assert l["sessionId"] == r["sessionId"] and l["incoming"] != r["incoming"]
        assert l["archiveReaderGrantId"] == r["archiveReaderGrantId"] == grant_scope(read_name)[1]
        # Public fixture bytes test opaque routing only. Real mutually
        # authenticated peer TLS is covered by archive_reliable_endpoint_test.
        wire = l["sessionId"].encode() + b"opaque-public-relay-fixture"
        write_frame(left, wire)
        assert read_frame(right) == wire
        write_frame(right, wire)
        assert read_frame(left) == wire
        return left, right
    except BaseException:
        left.close(); right.close()
        raise

running = False
try:
    lifecycle("start"); running = True
    assert not rpc("archive_authorization_state", auth=False).get("ok")
    assert not rpc("archive_authorization_state", device="f"*64).get("ok")
    if not resuming:
        assert not rpc("archive_grant_register", name="read").get("ok")
    accepted("archive_descriptor_register", "issuer-descriptor")
    accepted("archive_descriptor_register", "reader-descriptor", reader)
    accepted("archive_descriptor_register", "other-descriptor", other)
    assert not rpc("archive_grant_register", name="foreign").get("ok")
    accepted("archive_grant_register", "serve")
    accepted("archive_grant_register", "read")
    accepted("archive_grant_register", "read")
    assert not rpc("archive_grant_register", name="conflict").get("ok")
    before = state()
    assert len(before["devices"]) == 3 and len(before["grants"]) == 2
    assert all(row["currentlyEligible"] for row in before["grants"])
    invalid = relay_request(owner, reader); invalid["purpose"] = "friend"
    reject_relay(invalid)  # Account membership is not a friend/DM route.
    reject_relay(relay_request(other, reader))
    invalid = relay_request(owner, reader); invalid["archiveReaderGrantId"] = "0"*32
    reject_relay(invalid)
    invalid = relay_request(owner, reader); invalid["targetAccountId"] = "9"
    reject_relay(invalid)
    left, right = pair_relay()
    accepted("archive_grant_register", "revoke")
    for tls in (left, right):
        try:
            read_frame(tls)
            raise AssertionError("revoked archive relay remained open")
        except (EOFError, ssl.SSLError, ConnectionResetError):
            pass
        finally:
            tls.close()
    reject_relay(relay_request(owner, reader))
    assert not rpc("archive_grant_register", name="read").get("ok")
    revoked = state()
    target = next(row for row in revoked["grants"] if row["grantId"] == owner+":"+reader)
    assert not target["currentlyEligible"]
    accepted("archive_grant_register", "reenroll")
    left, right = pair_relay("reenroll")
    left.close(); right.close()
    lifecycle("stop"); running = False
    lifecycle("start"); running = True
    restarted = state()
    assert restarted["serverEpoch"] != before["serverEpoch"]
    assert len(restarted["grants"]) == 2 and all(row["currentlyEligible"] for row in restarted["grants"])
    lifecycle("stop"); running = False
    with sqlite3.connect(db_path) as db:
        db.execute("DELETE FROM devices WHERE public_identity=?", (reader,))
        db.execute("INSERT INTO devices(account_id,public_identity,created_utc) VALUES(8,?,1)", (reader,))
    lifecycle("start"); running = True
    assert not rpc("archive_grant_register", name="reenroll").get("ok")
    removed = state()
    target = next(row for row in removed["grants"] if row["grantId"] == owner+":"+reader)
    assert not target["currentlyEligible"]
    accepted("archive_grant_register", "reenroll-after-removal")
    target = next(row for row in state()["grants"] if row["grantId"] == owner+":"+reader)
    assert target["currentlyEligible"]
    print("PASS: Linux ARM32 real TLS/archive RPC and opaque relay; exact signed grant-pair admission, no friend/DM substitution, bidirectional pairing, active revocation closure, re-enrollment, restart and device-removal fail-closed.")
finally:
    if running:
        lifecycle("stop")
    with sqlite3.connect(db_path) as db:
        assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
