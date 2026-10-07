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
