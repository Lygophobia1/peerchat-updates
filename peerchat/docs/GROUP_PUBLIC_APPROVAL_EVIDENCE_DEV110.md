# Group public approval evidence — dev.110

Resumes the group verification roadmap after dev.107–109 Android direct-message
pictures/files, protected long text and durable recent attachment presentation.
Those product changes are preserved; physical multimedia validation remains pending.

Authenticated current member roster responses now include at most 32 public signed
device approval/revocation proofs, ordered by applied generation. The server reparses
the actual signed proof and compares its group namespace, actor/target/account,
proof ID, generation and revocation purpose with the stored database columns.
Proofs survive server restart. Revoked/nonmember devices cannot obtain a roster;
invited-only devices do not receive approval evidence. No keys or history are in
this readout.

The portable native evidence validator rejects signature damage, future/stale
applied epochs, wrong namespaces, duplicate events and reordered generations.
This helper does NOT admit an event or replace Replica's consecutive before/after
roster and signed-ledger admission. Durable per-event receipts, atomic rollback and
verified recovery remain intact. A truncated public evidence window is not a
complete ledger and must never be treated as one.

Production additional-device enrollment remains OFF. No issuing RPC/UI is enabled.
The client application still needs bounded evidence projection into native
snapshots, exact current-session binding, receipt reconciliation and explicit
approval/revocation UI before activation. Bulk roster evidence paging/batching must
also stay within control-channel payload/work bounds. No automatic historical
access is introduced.

Passed:

- Windows CNG and portable native signed identity/approval tests, individual durable
  admission, rollback, restart and default-closed behavior.
- Windows and portable server signature/account/device/epoch tests; approval,
  revocation and reenrollment; ordinary-member same-account approval; device
  removal/account removal; verified public evidence restart and ordering.
- Actual packaged native Create/root registration and earlier roster migration.
- 127 Android JVM/Compose tests, debug/test/unsigned release builds and lint.
- Full Windows build/import; version/Development updater extraction/runtime hashes;
  Android ARM64/JNI/dependency/release-hook audit; git diff --check.

No physical acceptance, install, deployment, merge or publication claimed.

Windows package:
`build/development/4.0.2-dev.110/DevelopmentUpdater/PeerChat-4.0.2-dev.110.zip`

SHA-256: `6f6ef7082a155c71b714499be767e475b0652d87b8d43f5ddc625c4c9ea9dc91`

Android packages under `build/development/4.0.2-dev.110/Android/`:

- Debug: `47a53b31c1eb617e8d8cd114f5a20f0a5c82a6e6621793ec04fb96c1387c7d09`
- Test: `e4fcac9fa788d8fe682b0a5cad85fbdbe66c6cd95aa9b356512033d5b6c9a8c5`
- Unsigned release: `ef6b88bd375deba176214fd3a6fc9de99c4dd33dc4ae4b5490c3885ef837c387`
