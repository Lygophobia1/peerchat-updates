# Network cleanup Phase 2

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
