# Linux server portability

Current ARM32 runtime milestone: 4.0.2-dev.33, source checkpoint
7cc8f159c44b23c3b1aa4ab825c1c3a01fed6d0a. The actual Raspbian 12 ARMv7 Pi
passed startup/TLS/SQLite/signatures/presence/mailbox/groups/RPC/shutdown parity.
See LINUX_ARM32_VALIDATION.md for hashes, commands, results and limitations.
Linux x64/ARM64 remain build-only candidates awaiting their own execution gates.

## Stage 3 prerequisite candidate: 4.0.2-dev.28

Parent checkpoint: 316bfc9 (bounded authenticated RPC reuse).

The modern headless project compiles the same production server sources, not a
second server implementation. Build with .NET SDK 10.0.401; runtime is pinned to
10.0.12. There are no additional NuGet packages.

Thin conditional adapters isolate Framework JavaScriptSerializer, Windows SQLite
linkage, CNG verification, DPAPI and FCM RSA signing. Windows Framework continues
using its existing implementations. Linux resolves SQLite to libsqlite3.so.0.
Modern SQLite text marshalling is explicitly UTF-8. Schema, WAL and mailbox FULL
durability remain unchanged.

Framework-generated compatibility fixtures cover JSON casing, Unicode, numeric
and timestamp handling, nulls, arrays, existing CNG public blobs, P1363 signatures,
Peer IDs, group proofs and mailbox canonical envelopes. JSON escaping may differ
while preserving parsed values; signed canonical binary formats do not change.
The portable serializer preserves ScriptIgnore for decrypted secret fields.

Linux secret loading requires absolute restricted files (no group/other access,
no leaf symlink, bounded size, strict UTF-8). TurnRestSharedSecretFile is the
portable alternative to Windows DPAPI, not a plaintext fallback. Conflicting
secret sources fail closed. Existing external FCM PKCS8 signing uses platform
RSA; credentials remain outside packages. Certificate validation accepts the
already-supported explicit PFX path as well as the Windows store thumbprint.

## Validation

- tests/test-server-portability.ps1: PASS. Framework/modern JSON and signature
  interoperability, portable DPAPI/RSA, roster proof/database, presence (1,000
  cycles), mailbox store/fetch/ACK, TTL, quota, rotation, revocation and restart.
- tests/test-discovery.ps1 against the modern Windows headless server: PASS.
  Includes authentication, bounded RPC reuse/retirement, authorization, device
  binding, initial presence, logout, lease expiry, replacement and restart.
- Framework presence and FCM provider regressions: PASS.
- Full Windows dev.28 build and package/import/version/updater audit: PASS.
- Android JVM, debug/test/release, lint and ARM64/JNI/TLS/HPKE audits: PASS.
- git diff --check: PASS before checkpoint.

Evidence directory: build/tests/server-portability-272f66c7ae644069bd321e142ce2635f.
Windows package: build/development/4.0.2-dev.28/DevelopmentUpdater/PeerChat-4.0.2-dev.28.zip.
SHA-256: 4253f93b943567ca06f96291018f28f2f520c6d2de5705223a582e8e77f14163.
Android: build/android/4.0.2-dev.28/PeerChat-4.0.2-dev.28-debug.apk.

## Remaining work (not accepted as Linux runtime parity)

No Linux runtime was available during these Windows-host tests. Actual Linux
x64/ARM64 execution, TLS/platform crypto/SQLite resolution, Unix permissions,
SIGTERM/SIGINT drain, explicit service config and Pi readiness remain Stage 4/5
gates. Modern Windows execution is not a substitute for Linux execution.

References:
- https://learn.microsoft.com/dotnet/standard/native-interop/native-library-loading
- https://learn.microsoft.com/dotnet/core/deploying/
