# Network cleanup Phase 2

Subsequent physical ARM32 server milestone: dev.33 / source checkpoint
7cc8f159c44b23c3b1aa4ab825c1c3a01fed6d0a. Manual Raspbian 12 ARMv7 execution
and automated production parity passed; see LINUX_ARM32_VALIDATION.md.
True ARM64/x64 execution and user Windows/Android physical acceptance remain pending.

## Phase 2A: authoritative live presence

Baseline: e5d04dc256b744a9d47853d40e0385524b91f388 / 4.0.2-dev.22.
Candidate: 4.0.2-dev.24. Dev.23 is an intermediate artifact, not this checkpoint.

The existing server owns an ephemeral per-device presence registry. A valid
authenticated event subscription binds its lease to a unique owner and session.
Renewal uses a 75-second monotonic deadline, not wall time or a SQLite heartbeat
write. Each server start has a new epoch; replacements have new generations.
Cleanup is conditional on the owner/generation so an old connection cannot
remove a replacement. Known EOF/logout clears promptly; silent loss expires.
Expired leases cannot be resurrected by late event heartbeats.

Account Online means at least one authorized live device. Durable registration,
revocation, identity and recipient keys remain in SQLite; disconnect does not
delete them or mailbox ciphertext/receipts. Existing legacy presence rows are
retained, but cannot make an account online.

Event establishment includes an authorized friend snapshot before notifications.
Clients reject duplicate/reordered presence revisions and old epochs. Reconnect
uses the existing bounded authoritative friend-state refresh, not contact polling.
Windows friend presence now has a separate projection from native route state;
Android retains its server-derived friend projection. Dormant native retry flags
are not projected as active retry without route demand. No new route owner exists.

Validation:

- tests/test-network-presence.ps1: monotonic lease, multi-device aggregation,
  stale owners/generations, logout/expiry/revocation/restart, 1,000 lifetimes,
  replacement during expiry, actual database reopen and zero persisted heartbeat
  rows, client revision guard, offline composer and presence/route independence.
- tests/test-discovery.ps1: auth/ownership, initial online friend snapshot,
  clean disconnect, silent expiry, replacement, restart and administrative tests.
- Mailbox protocol, three-client encrypted fanout, remembered authentication and
  1,000 relay-demand/idle lifecycle regressions passed.
- Android JVM tests, debug/test/release builds and lint passed.
- Full Windows build, package/import, Android ARM64/JNI/TLS dependency audits and
  Windows/Android version/updater extraction audit passed.

Windows runtime: build/development/4.0.2-dev.24/Package.
Development-only updater: build/development/4.0.2-dev.24/DevelopmentUpdater.
ZIP SHA-256: f4120ae9da17c0316d7fd62c6afe714df5ac0c20f386756c81bced82b0bc477d.
Android debug APK archive: build/android/4.0.2-dev.24.
No feed publication, merge or device acceptance is claimed.

Physical pending: already-online contact, idle route while Online, network loss,
multi-device account aggregation and Windows/Android reconnect against this build.
Existing dev.22 server/user profiles/keys were not modified.

## Phase 2B: bounded authenticated RPC reuse

Presence checkpoint: d423001. RPC candidate: 4.0.2-dev.27; dev.25/dev.26
are preserved interim builds, not the completed RPC checkpoint.

The server advertises rpcVersion=1 additively. Old clients retain one-shot
requests; new clients retain that behavior against servers without capability.
After device registration/bootstrap, each profile's existing DiscoveryClient or
ServerAccounts owns one serialized request channel. The authenticated rpc_open
binds account session and device. Every frame revalidates the session/device.
Event, relay and peer-TLS channels remain separate. No authentication, event,
relay or remembered-proof operation can be dispatched through this channel.
Ordinary payloads retain 4 KiB limits; opaque mailbox frames retain 100,000-byte
limits. RPC correlation uses rpcRequestId, never application requestId.

Server limits: 15-second read/idle timeout, 64 requests, two-minute lifetime.
Clients retire after 10 seconds idle, 60 requests or 100 seconds age. Operations
and serialization waits are bounded; cancellation closes Android raw sockets
through the existing OperationScope. Background quiescence, logout and backend
disposal close RPC resources. A wake does not retain an idle background socket.
No reconnect runs while idle, and ambiguous failures are never silently replayed.
The existing delivery/ACK/dedupe owners decide any subsequent retry.

Regression additions cover serialized reuse/connection count, profile binding,
idle/count retirement, externally closed resources, failure invalidation, partial
frames, wrong-device binding and reconnect on demand. The complete discovery
suite retains P2P consent/request IDs, account/device authorization, logout and
revocation checks. Eight sequential ordinary requests use one TLS connection;
80 requests cross the count bound using at most two additional connections.
This is a connection-count measurement, not a battery-saving claim.

Android JVM/build/lint and ARM64/JNI/TLS/HPKE artifact audits, native mailbox
crypto/store/fetch/ACK/restart/sender-independence, remembered-device authentication,
route-demand tests, full Windows build/import audit and version/updater audit pass.
Physical foreground/background/locked mailbox and presence/reconnect remain
pending against the final source; no device success is inferred from host tests.

Windows: build/development/4.0.2-dev.27/Package.
Android: build/android/4.0.2-dev.27/PeerChat-4.0.2-dev.27-debug.apk.
Development updater ZIP SHA-256:
d9c07b6af1be129d815baa9d818e2028a89ebef2abbe519fe2f68812258fe4af.
No publication or merge.

## Server portability checkpoints

Stage 1 (presence): d423001 / dev.24.
Stage 2 (bounded RPC): 316bfc9 / dev.27.
Stage 3 (portable platform prerequisites): 9c07df7 / dev.28.
Stage 4 candidate: dev.32. Linux runtime acceptance is still pending.

The candidate adds Linux explicit-path/secret/PFX rules, POSIX signal handlers,
shared bounded socket/worker/push drain and checked SQLite close, busy handling,
FULL/WAL checks, and fail-closed migration/open errors without schema changes.
Modern SslStream buffering exposed a real event-loop readiness assumption;
the single pending framed-read correction passes the original room reconnect
assertions. Desktop event stop closes its owned socket to unblock its worker.
No messaging, session or media ownership has moved.

Dev.32 host results: modern full discovery/auth/presence/RPC/restart PASS;
portable fixtures and production mailbox/roster/group/room tests PASS;
Framework mailbox/fanout/presence/remembered regressions PASS; full Windows
build/package/import PASS; Android 87 JVM tests, debug/test/release and lint PASS;
ARM64/JNI/TLS/HPKE artifact audits PASS; version/updater extraction/hash PASS.
Framework full discovery compatibility PASS, including admin and restart.
No physical-device success is inferred from these tests.

Portable evidence: build/tests/server-portability-875dfd00171b4008aad4bfe8a40f3b43.
Native/Framework mailbox evidence: build/tests/mailbox-protocol-6e8c86c2bcee45e688874f33cba7e7e8.

Windows updater: build/development/4.0.2-dev.32/DevelopmentUpdater/PeerChat-4.0.2-dev.32.zip.
SHA-256: e39ace7637c45a814fbf00f5d70723d3d4e15345e56398509087dfc4fce09598.
Android: build/android/4.0.2-dev.32/PeerChat-4.0.2-dev.32-debug.apk.
Linux x64: build/linux/4.0.2-dev.32/PeerChat.Server-4.0.2-dev.32-linux-x64.zip.
SHA-256: 5972b9fdf7ff981f945130c38d9ae67212df83f62ceb57ec2258c0b050704e90.
Linux ARM64: build/linux/4.0.2-dev.32/PeerChat.Server-4.0.2-dev.32-linux-arm64.zip.
SHA-256: 37e02c0b1073c0195968fad245d029352a0412d6da9fb11fd6d6d6729d32f5e7.
Linux packages contain only managed RID-targeted DLL/deps/runtimeconfig; .NET
10.0.12 and libsqlite3.so.0 plus distro runtime dependencies are external.

Earlier dev.29-.31 Linux artifacts are intermediate build/audit attempts, not
accepted candidates. An ARM64 execution check on Windows x64 correctly failed
architecture loading and was replaced with explicit static architecture/version
inspection, not presented as runtime success. One native synthetic CNG fixture
attempt failed; fresh reruns and the full native mailbox regression passed with
no cryptographic production changes. Reusing an already-populated test directory
also correctly hit fixture uniqueness; all final tests used fresh directories.

No Linux runtime/WSL distribution/container/emulator is available here. Therefore
actual Linux x64/ARM64 platform crypto, SQLite resolution, permissions, TLS,
signals and end-to-end parity are not claimed. LINUX_ARM64_VALIDATION.md lists
the exact remaining gates. RASPBERRY_PI_DEPLOYMENT.md and deployment templates
are readiness drafts only, not completed Stage 5 deployment acceptance.

Physical Windows/Android presence/RPC and foreground/background/locked mailbox
must be repeated on the final source. The user's existing dev.22 server was left
running unchanged; this is not evidence of dev.32 physical server acceptance.
No user data/configuration/secrets/identities were modified. Authoritative main,
accepted dev.22 and existing packages were preserved. The three pre-existing
untracked screen .obj files remain excluded from all checkpoints.
