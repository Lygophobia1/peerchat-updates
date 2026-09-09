"""Six real Windows processes using the exact Node core linked into the GUI.

Only synthetic profiles and loopback connections are used. Test identities are
explicitly approved when their known IDs match. No friend/user data is accessed.
"""
from pathlib import Path
import queue
import socket
import subprocess
import threading
import time
import uuid

PROJECT_ROOT = Path(__file__).resolve().parent.parent
BIN_ROOT = PROJECT_ROOT / 'build' / 'tests' / 'core'


def available_port():
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        return listener.getsockname()[1]


class Peer:
    def __init__(self, directory, port):
        self.process = subprocess.Popen(
            [str(BIN_ROOT / 'peer-harness.exe'), str(directory), str(port)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding='utf-8', bufsize=1)
        self.lines = queue.Queue()
        self.identity = None
        self.connected = set()
        self.received = set()
        self.delivered = set()
        self.histories = {}
        self.contact_count = 0
        self.errors = 0
        def read():
            for line in self.process.stdout:
                self.lines.put(line.strip())
        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()

    def command(self, text):
        self.process.stdin.write(text + '\n')
        self.process.stdin.flush()

    def drain(self, identities):
        while not self.lines.empty():
            words = self.lines.get().split()
            if not words:
                continue
            action = words[0]
            if action == 'READY':
                self.identity = words[1]
            elif action == 'TRUST':
                assert words[1] in identities, 'Unexpected identity requested trust'
                self.command('APPROVE ' + words[1])
            elif action == 'CONNECTED':
                self.connected.add(words[1])
            elif action == 'DISCONNECTED':
                self.connected.discard(words[1])
            elif action == 'MESSAGE' and words[3] != self.identity:
                assert words[2] not in self.received, 'Duplicate incoming event'
                self.received.add(words[2])
            elif action == 'DELIVERED':
                self.delivered.add(words[1])
            elif action == 'HISTORY':
                self.histories[words[1]] = int(words[2])
            elif action == 'CONTACTS':
                self.contact_count = int(words[1])
            elif action == 'ERROR':
                self.errors += 1
            elif action == 'PAYLOAD_ERROR':
                raise AssertionError('Unicode/URL payload did not survive transport')

    def close(self):
        if self.process.poll() is None:
            self.command('QUIT')
            self.process.wait(timeout=10)
        assert self.process.returncode == 0, 'Unclean shutdown'
        self.reader.join(timeout=2)
        self.process.stdin.close()
        self.process.stdout.close()


def main():
    run = PROJECT_ROOT / 'build' / 'tests' / 'runtime' / ('six-processes-' + uuid.uuid4().hex)
    ports = [available_port() for _ in range(6)]
    assert len(set(ports)) == 6
    peers = []
    identities = set()

    def wait(condition, explanation, timeout=30):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for peer in peers:
                if peer.process.poll() is None:
                    peer.drain(identities)
            if condition():
                return
            time.sleep(.02)
        raise AssertionError(explanation)

    try:
        peers.extend(Peer(run / str(index), port) for index, port in enumerate(ports))
        wait(lambda: all(peer.identity for peer in peers), 'Listeners failed to start')
        identities.update(peer.identity for peer in peers)
        for source in range(6):
            for target in range(source + 1, 6):
                peers[source].command(f'CONNECT localhost {ports[target]} {peers[target].identity}')
        wait(lambda: all(len(peer.connected) == 5 for peer in peers), 'Mutual verification/connect failed')
        for source in range(6):
            for target in range(6):
                if source != target:
                    peers[source].command('SEND ' + peers[target].identity)
        wait(lambda: all(len(peer.received) == 5 and len(peer.delivered) == 5 for peer in peers), 'Text/ACK delivery failed')
        print('PASS: six processes, 15 direct TLS links, 30 Unicode/link messages and ACKs')

        # A malicious unauthenticated connection must not crash the listener.
        with socket.create_connection(('127.0.0.1', ports[0]), timeout=3) as hostile:
            hostile.sendall(b'not TLS\x00' * 100)
        peers[1].command('SEND ' + peers[0].identity)
        wait(lambda: len(peers[0].received) == 6, 'Malformed connection disrupted a valid peer')
        print('PASS: malformed transport does not interrupt existing conversations')

        restart_identity = peers[5].identity
        peers[5].close()
        wait(lambda: all(restart_identity not in peer.connected for peer in peers[:5]), 'Disconnect not detected')
        peers[0].command('SEND ' + restart_identity)
        peers[5] = Peer(run / '5', ports[5])
        wait(lambda: peers[5].identity == restart_identity and len(peers[5].connected) == 5, 'Persistent identity/reconnect failed')
        wait(lambda: len(peers[5].received) == 1 and len(peers[0].delivered) == 6, 'Offline outbox delivery failed')
        peers[5].command('HISTORY ' + peers[0].identity)
        wait(lambda: peers[5].histories.get(peers[0].identity) == 3, 'History after restart missing or duplicated')
        assert peers[5].contact_count == 5
        print('PASS: clean restart, saved contacts, offline outbox, history reload without duplicates')
    finally:
        for peer in peers:
            try:
                peer.close()
            except Exception:
                peer.process.kill()
                peer.process.wait()
                raise


if __name__ == '__main__':
    main()
