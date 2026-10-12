# Durable group replica checkpoint — dev.105

This prerequisite does not activate production device enrollment. The existing
same-account approval policy is unchanged; no new policy decision is needed.

## Ownership and admission

`group_devices::Replica` copies the signed ledger, validates an exact consecutive
authenticated roster transition and the existing double-signed device proof, and
admits only that proof's event ID. One approval never enables arbitrary device
events. The group namespace remains bound to the accepted original Create event.
The caller must still supply authenticated current server snapshots; RPC/session
evidence projection and explicit application approval UI are not complete here.

Storage commits the protected ledger event and protected verification receipt in
one existing SQLite transaction. Only after commit does the live ledger change.
No schema, identity, key, history, outbox or ACK ownership changes are introduced.
Conflicting receipts/events fail closed; exact storage duplicates are idempotent.

Windows Node startup now uses the reusable recovery boundary. Ordinary groups keep
their existing accepted-event replay behavior. Persisted device events require a
protected receipt, exact event correspondence, verified signatures and consecutive
roster/ledger reconciliation on every reopen. Recovery does not establish a live
route, authorize historical keys, enable new enrollment or replace current-roster
checks. Android compiles the same boundary but does not yet expose group application
commands through JNI.

## Automated evidence

- CNG and portable native/server enrollment tests: proof-specific admission,
  stale/replayed transition rejection, revocation, fresh explicit reenrollment,
  16 storage/application recovery cycles, missing/conflicting receipts, unchanged
  account/device security and both server verifier implementations.
- Production record-provider failure after event insertion rolls back both event
  and receipt. Reopen confirms no partial state; exact duplicate commit succeeds
  and conflicting commit preserves original evidence.
- Full portable Storage regressions: identity/CNG/DPAPI, 1,000 opaque signing
  cycles, SQL rollback/restart/concurrency, protected records, Profile, key registry,
  history/outbox/ACK/dedupe and encrypted group envelope/provenance tests.
- Group ciphertext fanout and packaged group registration/migration passed.
- History settings and real first-enrollment WPF tests passed. Paired native archive
  mutual TLS/import and five worker restart checks passed. No physical acceptance
  is claimed for this new group boundary.
- Windows full build, runtime/import/media ABI audit passed. Android 120 JVM/UI
  tests, debug/test/release builds and lint passed; ARM64/JNI/dependency/license and
  release test-hook exclusion audits passed. Existing compiler deprecation/async
  warnings remain.
- Version/updater/extraction audit passed under Windows PowerShell/.NET Framework.
  The first PowerShell Core invocation could not deserialize the Framework updater
  manifest; no product change was needed. Its artifact is preserved separately.

## Unpublished reference artifacts

Canonical validated Windows archive:
`build/development/4.0.2-dev.105/DevelopmentUpdater-FrameworkValidation/PeerChat-4.0.2-dev.105.zip`

SHA-256: `0a51fea040e545db9a099c8479e4ed2a326324d35c0aa0e8bd18697b3e677f66`

Android artifacts are preserved under `build/development/4.0.2-dev.105/Android`:

- Debug: `ed2756e55e9191f0a6d47d441674a2e894f6ddf1dee7d8b06781c9dbed1c870b`
- Test: `35f546b39995c8f2aa24512f91d8e9139085090d9eff793cf66f46e9c3d57009`
- Unsigned release: `099a8943dcbce7e76c437fdc2ea05f4801863074e831cbdfaa5d20c3398210b7`

Development unsigned policy only. No publication, deployment, installation or
merge. Unrelated object files remain excluded. Continue authenticated roster/root
and receipt projection, exact application-session admission and explicit same-account
approval UI before activating enrollment. Later group/Android/media milestones
remain unfinished.
