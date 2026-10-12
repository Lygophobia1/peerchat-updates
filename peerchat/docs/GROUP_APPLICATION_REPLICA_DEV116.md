# Durable group replica application integration — dev.116

The existing Node authenticated-roster command now invokes the native Replica
application boundary. GroupKeyManager's admission preflight is pure: it validates
the candidate without changing keys, handoffs or persistence. The existing Node
account/discovery-generation checks and SessionTransportManager remain owners.
No second session, transfer, retry or group-state engine is introduced.

With enrollment explicitly enabled in isolated tests, a device-set transition
requires consecutive authenticated before/after snapshots, the exact signed proof
for that applied generation, an authorized signed local ledger and byte-exact
creation-root agreement. The existing Replica atomically persists event + protected
receipt before live key authority is advanced. Repeated snapshots do not duplicate
events. Reopen reconstructs individually admitted events from verified protected
receipts, not from a current roster or truncated public evidence window.

Missing proofs/ledgers, epoch gaps, role substitution, conflicting signed roots
and receipt persistence failures reject admission. Failure removes usable roster
key authority, leaves uncommitted live ledger transitions unchanged and preserves
existing stored evidence. No key distribution occurs in this helper.

Production enrollment is STILL DISABLED: Node calls the boundary with the default
OFF policy, and constructs GroupKeyManager with its default OFF policy. No issuing
RPC or approval UI has been enabled. Missing-epoch/full-ledger recovery, newly
approved sibling bootstrap, same-account control/key provisioning and the explicit
approval/revocation UI remain necessary before activation. Existing registered
one-device-per-account groups continue through the same application command.

Tests passed:

- Windows CNG and portable signing: actual application admission, pure preflight,
  duplicate snapshots, revoke/reenroll, sixteen protected application reopen cycles,
  disabled enrollment, missing proof/ledger, stale epoch gaps, role substitution,
  a validly signed conflicting creation root and conflicting receipt persistence.
- Existing individual receipts/rollback/restart and server account/device/session,
  server epoch, revocation, concurrency, removal and bounded evidence regressions.
- Group envelope/control tests: three non-friend device workers, mutual TLS,
  per-device key handoff, encrypted text, duplicate prevention, protected history
  reload/pagination and removal key rotation. Host tests, not physical acceptance.
- TransferManager binary/Unicode, cancellation, peer binding, bounds and restart;
  protected attachment catalog/reopen/scoped archive attestation tests.
- Actual paired Node archive workers and verified attachment download through one
  TransferManager; packaged archive C ABI and five profile-worker restarts.
- Packaged group creation/proof contracts and chat attachment/long-text UI,
  scrollbar, Unicode/CRLF, thumbnails and preview bounds.
- 127 Android JVM/Compose tests, debug/test/unsigned-release builds, lint,
  ARM64/JNI/dependency/release-hook audit; full Windows build/package/import audit;
  version/Development updater extraction/runtime hashes; git diff --check.

dev.115 is an unpublished intermediate candidate. dev.116 includes exact signed
creation-root matching; immutable previous artifacts were not overwritten.
Android DM pictures/files/protected long text/recent durable attachments from
dev.107–109 remain present. Their physical Windows ↔ Motorola acceptance is pending.
No device install, deployment, merge, publication, key replacement or history/grant
deletion occurred. Unrelated generated object files remain excluded.

Windows package:
`build/development/4.0.2-dev.116/DevelopmentUpdater/PeerChat-4.0.2-dev.116.zip`

SHA-256: `e44a207a90e295a0e65e73604a852e131769bc1950ad4c18cac6407840f80c25`

Android packages: `build/development/4.0.2-dev.116/Android/`

- Debug: `0b578e19d4c00fa831062b564f30a3cdaab2c753fa9a182af4322a2d5329df02`
- Test: `e4fcac9fa788d8fe682b0a5cad85fbdbe66c6cd95aa9b356512033d5b6c9a8c5`
- Unsigned release: `bbf3f6e44ccd2d7f228fccc63885216e8b4444102d912c666b5937465d5ea5fd`
