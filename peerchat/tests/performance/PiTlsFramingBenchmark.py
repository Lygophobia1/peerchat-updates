"""Ephemeral Linux loopback TLS framing benchmark; no production files/keys.

Pipe this source over the existing authorized SSH connection to python3 -.
Uses only the installed Python/OpenSSL and RAM-only generated fixture material.
It neither connects to nor changes PeerChat.Server or coturn.
"""
import os
import socket
import ssl
import statistics
import subprocess
import threading
import time


def exact(stream, count):
    result = b""
    while len(result) < count:
        part = stream.recv(count - len(result))
        if not part:
            raise RuntimeError("early EOF")
        result += part
    return result


def read_frame(stream):
    size = int.from_bytes(exact(stream, 4), "big")
    if not 1 <= size <= 65536:
        raise RuntimeError("frame bound")
    return exact(stream, size)


def write_frame(stream, payload, joined):
    header = len(payload).to_bytes(4, "big")
    if joined:
        stream.sendall(header + payload)
    else:
        stream.sendall(header)
        stream.sendall(payload)


key_fd = os.memfd_create("peerchat-framing-fixture-key", os.MFD_CLOEXEC)
cert_fd = os.memfd_create("peerchat-framing-fixture-cert", os.MFD_CLOEXEC)
try:
    subprocess.run([
        "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
        "-keyout", "/proc/self/fd/" + str(key_fd),
        "-out", "/proc/self/fd/" + str(cert_fd),
        "-subj", "/CN=localhost", "-days", "1",
    ], pass_fds=(key_fd, cert_fd), stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE, check=True, timeout=15)
    server_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    server_context.minimum_version = server_context.maximum_version = ssl.TLSVersion.TLSv1_2
    server_context.load_cert_chain("/proc/self/fd/" + str(cert_fd), "/proc/self/fd/" + str(key_fd))
    # Trust ONLY this generated fixture, not arbitrary certificates.
    client_context = ssl.create_default_context(cadata=os.pread(cert_fd, 65536, 0).decode("ascii"))
    client_context.minimum_version = client_context.maximum_version = ssl.TLSVersion.TLSv1_2
    for no_delay, joined in [(False, False), (True, False), (False, True)]:
        with socket.socket() as listener:
            listener.settimeout(15)
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            errors = []

            def serve():
                try:
                    raw = listener.accept()[0]
                    raw.settimeout(15)
                    raw.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, int(no_delay))
                    with server_context.wrap_socket(raw, server_side=True) as stream:
                        for _ in range(25):
                            if len(read_frame(stream)) != 32768:
                                raise RuntimeError("payload bound")
                            write_frame(stream, bytes(64), joined)
                except Exception as error:
                    errors.append(type(error).__name__)

            worker = threading.Thread(target=serve)
            worker.start()
            costs = []
            try:
                raw = socket.create_connection(listener.getsockname(), timeout=15)
                raw.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                with client_context.wrap_socket(raw, server_hostname="localhost") as stream:
                    for index in range(25):
                        started = time.monotonic()
                        write_frame(stream, bytes(32768), True)
                        if len(read_frame(stream)) != 64:
                            raise RuntimeError("ACK bound")
                        if index >= 5:
                            costs.append((time.monotonic() - started) * 1000)
            finally:
                worker.join(16)
            if errors or worker.is_alive():
                raise RuntimeError("bounded fixture failed " + str(errors))
            print("PI_LOOPBACK_TLS_FRAMING no_delay=%s joined=%s samples=%d mean_round_ms=%.3f max_round_ms=%.3f" %
                  (no_delay, joined, len(costs), statistics.mean(costs), max(costs)))
finally:
    os.close(key_fd)
    os.close(cert_fd)
