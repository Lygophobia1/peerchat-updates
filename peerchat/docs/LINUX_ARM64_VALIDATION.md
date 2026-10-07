# Linux x64 / ARM64 candidate validation

Status for x64/ARM64: CROSS-BUILD AND WINDOWS-HOST PARITY ONLY.
ARM32 Pi execution is separately validated at dev.33; see LINUX_ARM32_VALIDATION.md.
This does not establish x64 or ARM64 runtime acceptance.
Candidate: 4.0.2-dev.32, based on Stage 3 checkpoint 9c07df7.

Build with scripts/build-linux-server.ps1 using .NET SDK 10.0.401. It refuses
existing output directories, publishes linux-x64 and linux-arm64, verifies PE
target architecture, embedded VersionInfo, RID and runtime pin, and permits only
the server DLL/deps/runtimeconfig files. There is no apphost or bundled runtime.

Artifacts: build/linux/4.0.2-dev.32/PeerChat.Server-4.0.2-dev.32-linux-x64.zip
and PeerChat.Server-4.0.2-dev.32-linux-arm64.zip. Per-file SHA-256 manifests sit
beside them. ARM64 cannot execute in the Windows x64 process; metadata inspection
is explicitly not runtime validation. Earlier dev.29-.31 Linux folders are
intermediate attempts, not accepted packages, and were not overwritten.

## Shared lifecycle and persistence

The same server core tracks accepted socket workers, presence and admin workers.
Shutdown stops accepting, closes sockets/admin pipe, drains application workers
within 20 seconds and push work within a further 10 seconds, then checks SQLite
close success. A drain timeout does not close a database underneath active work;
startup/main returns failure and process termination provides transaction abort.
Committed mailbox durability does not depend on orderly shutdown. Linux registers
SIGTERM/SIGINT; Windows retains its local admin stop and handles console cancel.

SQLite keeps WAL and explicit FULL synchronous mode, adds a five-second busy
timeout, checks close status and releases handles after failed opens/schema
initialization. Existing migrations ignore only a duplicate-column error, not
arbitrary corruption/I/O errors. Schema and encryption are unchanged.

Linux requires an existing absolute --config path, an explicit absolute data root,
absolute database/log/PFX paths, and no Windows certificate-store/admin-pipe
settings. PFX keys use ephemeral import. PFX, TURN and FCM credential files must
be restricted; the optional CertificatePasswordFile avoids inline passwords.
No existing Windows certificate, configuration or secret has been changed.

## Regression discovered during parity work

Modern SslStream may retain decrypted frames after socket readiness is consumed.
The old server event loop used socket Poll/Available before each framed read,
stranding a queued room snapshot after reconnect. The modern room test failed
with connected=True and Pending: snapshot. A single outstanding asynchronous
framed read now consumes buffered TLS data while the existing lease owner checks
its deadlines. No second reader or reconnect owner was added. The unchanged
room assertions pass after the correction. Desktop stop also closes its owned
event socket to unblock an outstanding read before waiting for its worker.

## Host gates

Run tests/test-server-portability.ps1 in a fresh unique test directory. It includes:
Framework/modern JSON and CNG/P1363/Peer-ID compatibility; malformed/tampered
signature rejection; portable RSA and Windows DPAPI; 1,000 presence lifetimes;
50 SQLite Unicode/open/reopen cycles; WAL/FULL/busy/corrupt DB handling; bounded
push drain; five idle TLS/partial-frame shutdown/restart cycles; three-client
group ciphertext fanout, room authorization/reconnect, and sender-logged-out
mailbox fetch/ACK/receipt reconciliation, quotas/TTL/revocation/restart.

Also run the full discovery harness against BOTH the modern Windows host and
Framework build, plus normal native mailbox/remembered/presence regressions,
full Windows build/import/package/version audits and Android JVM/build/lint/
ARM64/JNI/TLS/HPKE audits. Detailed final results are in NETWORK_CLEANUP_PHASE2.md.

## Required Linux runtime gate

No WSL distribution, Docker/Podman or ARM emulator is available on this machine.
No system features were installed and no reboot/real deployment was attempted.
Obtain an isolated Linux x64 and ARM64 host with .NET 10.0.12 plus its distro
dependencies and libsqlite3.so.0 (glibc Linux, not Alpine/musl).

On each host, verify dotnet --info, uname -m, ZIP/manifests, then execute:
dotnet /absolute/path/PeerChat.Server.dll --version
dotnet /absolute/path/PeerChat.Server.dll --config /absolute/test/server-config.json

Use synthetic fixture data/certificates only. Repeat registration/login,
remembered proofs, presence/epoch, events including coalesced frames, relay
authorization, group roster/fanout, mailbox sender-independent store/fetch/ACK,
receipt persistence, malformed frames, TURN issuance and FCM construction.
Verify restricted-file and invalid-PFX failures, clean SIGTERM/SIGINT with active
event/relay/partial-frame work, forced process termination with committed Stored
items, SQLite WAL/reopen/integrity and same fixture signature/canonical bytes.
Do not copy production secrets into the test package.

Physical Windows/Android foreground/background/locked mailbox and presence/RPC
reconnect on this final source remain pending. No battery-saving claim is made.
