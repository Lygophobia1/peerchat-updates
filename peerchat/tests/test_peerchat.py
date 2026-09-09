"""Local process/socket integration tests; Python is only used for testing."""
import pathlib
import queue
import socket
import subprocess
import tempfile
import threading
import time

EXE = str(pathlib.Path(__file__).resolve().parent.parent / 'build' / 'legacy' / 'peerchat-cli.exe')

def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]

class Peer:
    def __init__(self, *args):
        self.p = subprocess.Popen([EXE, *map(str, args)], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.output = bytearray()
        def read():
            while True:
                b = self.p.stdout.read(1)
                if not b: break
                self.output.extend(b)
        threading.Thread(target=read, daemon=True).start()
    def wait_text(self, text):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if text.encode() in self.output: return
            time.sleep(.02)
        raise AssertionError(f'Missing {text!r}: {bytes(self.output)!r}')
    def send(self, text):
        self.p.stdin.write(text.encode() + b'\n'); self.p.stdin.flush()
    def close(self):
        if self.p.poll() is None: self.p.kill()
        self.p.wait(timeout=3)
        self.p.stdin.close(); self.p.stdout.close()

def exact(s, n):
    data = b''
    while len(data) < n:
        chunk = s.recv(n-len(data))
        if not chunk: raise EOFError()
        data += chunk
    return data

def run():
    with tempfile.TemporaryDirectory() as tmp:
        key = pathlib.Path(tmp)/'a.key'; wrong = pathlib.Path(tmp)/'b.key'
        for k in (key, wrong): subprocess.run([EXE, 'keygen', str(k)], check=True, capture_output=True)
        assert len(key.read_bytes()) == 32
        assert subprocess.run([EXE, 'keygen', str(key)], capture_output=True).returncode != 0
        for mode in ('chat', 'wrong-key', 'tamper', 'replay', 'oversize'):
            peers = []; errors = queue.Queue(); listener = None
            try:
                server_port = port()
                server = Peer('listen', '127.0.0.1', server_port, key); peers.append(server)
                server.wait_text('Waiting on')
                client_port = server_port
                if mode in ('tamper', 'replay', 'oversize'):
                    listener = socket.socket(); listener.bind(('127.0.0.1', 0)); listener.listen()
                    client_port = listener.getsockname()[1]
                    def proxy():
                        try:
                            with listener.accept()[0] as downstream, socket.create_connection(('127.0.0.1', server_port)) as upstream:
                                downstream.settimeout(5); upstream.settimeout(5)
                                upstream.sendall(exact(downstream, 36))
                                downstream.sendall(exact(upstream, 36))
                                h = exact(downstream, 4); proof = h + exact(downstream, int.from_bytes(h, 'big')+16)
                                upstream.sendall(proof)
                                h = exact(upstream, 4); downstream.sendall(h + exact(upstream, int.from_bytes(h, 'big')+16))
                                h = exact(downstream, 4); frame = bytearray(h + exact(downstream, int.from_bytes(h, 'big')+16))
                                assert b'secret payload' not in frame
                                if mode == 'tamper':
                                    frame[-1] ^= 1; upstream.sendall(frame)
                                elif mode == 'replay': upstream.sendall(proof)
                                else: upstream.sendall(b'\xff\xff\xff\xff')
                                # Keep the transport alive until the receiver rejects the frame.
                                try:
                                    while upstream.recv(1024): pass
                                except ConnectionResetError:
                                    pass  # Fail-closed process exit can reset TCP on Windows.
                        except Exception as e: errors.put(e)
                    worker = threading.Thread(target=proxy, daemon=True); worker.start()
                client = Peer('connect', '127.0.0.1', client_port, wrong if mode=='wrong-key' else key); peers.append(client)
                if mode == 'wrong-key':
                    for p in peers:
                        assert p.p.wait(timeout=8) != 0
                        assert b'Encrypted connection authenticated' not in p.output
                else:
                    for p in peers: p.wait_text('Encrypted connection authenticated')
                    client.send('secret payload')
                    if mode == 'chat':
                        server.wait_text('friend> secret payload')
                        server.send('hello back'); client.wait_text('friend> hello back')
                        client.send('/quit'); server.wait_text('Friend left')
                        assert server.p.wait(timeout=5)==0
                    else:
                        assert server.p.wait(timeout=8) != 0
                        assert b'friend> secret payload' not in server.output
                        worker.join(timeout=6)
                        assert not worker.is_alive()
                        if not errors.empty(): raise errors.get()
                print(f'PASS: {mode}')
            finally:
                for p in peers: p.close()
                if listener: listener.close()
        print('All integration tests passed.')

if __name__ == '__main__': run()
