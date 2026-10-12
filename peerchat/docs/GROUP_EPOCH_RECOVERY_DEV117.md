# Verified epoch recovery and metadata bootstrap — dev.117

Production enrollment remains OFF. This checkpoint completes bounded native
recovery primitives and durable server epoch evidence; it does not enable an
issuing RPC, approval UI or new group-key provisioning.

The server captures public before/after rosters in the SAME SQLite transaction
as the verified signed device proof. Recovery reads require the current exact
account/device/session lease and server epoch, plus current group membership.
Responses are ordered and paged within an 8 KiB evidence budget. Missing old
snapshots and ordinary membership gaps are explicitly unavailable; no historical
roster is guessed or backfilled from current membership. Existing data is retained.

Native application recovery checks exact initial/final snapshots, every signed
consecutive transition, accepted creation root, actor/device/account mapping,
roles and epoch continuity. It validates the entire bounded page before atomic
protected event/receipt persistence. Late persistence failure rolls back the
whole page and does not advance the live ledger or leave stale key authority.

A new sibling can bootstrap from bounded, ordered, verified membership metadata
and individually admitted device proofs. It requires exact authenticated terminal
membership/epoch and signed-root agreement. This boundary imports NO old messages,
attachments, history grants or content keys. Existing local groups cannot be
overwritten through the bootstrap entry point.

Focused Windows/portable crypto and actual Windows/portable server database tests
passed: valid multi-epoch recovery, missing/altered epochs, wrong roots, role
substitution, terminal mismatch, page bounds, late transaction rollback,
metadata-only sibling bootstrap, disabled gates, revoke/reenroll, protected
reopen, exact session replacement, server restart and bounded server pagination.
Portable Storage/identity/DPAPI/history/outbox/ACK/dedupe regressions and native
TransferManager/restart regressions passed.

Still required before production activation: authenticated recovery transport
and full membership-ledger provisioning across ordinary roster gaps, explicit
same-account approval/revocation UI, fresh current-key provisioning and actual
application enrollment/revocation/reconnect gates. Current read-only public proof
windows do not substitute for complete bootstrap evidence.

Physical Android multimedia testing has since exposed finalization, pacing and
large-text presentation regressions. Multimedia acceptance is FAILED/PENDING
RETEST. The older dev.116 candidate remains preserved as reference, not accepted.
The immediate priority is their correction before continuing group activation.
No existing profile, key, identity, history or grant was modified or deleted.

Final gates passed: Windows CNG and portable signing/server tests; full Windows
build/package/import audit; 127 Android JVM/Compose tests (zero failures/errors),
debug/test/unsigned-release builds, lint; ARM64/JNI/dependency/release-hook audit;
version consistency and dev.116 → dev.117 Development updater extraction/runtime
hash audit. Unsigned Development policy only, no publication or deployment.

Windows updater ZIP SHA-256:
`b8565e2ac514e903c2465af9eb4134b36ebbef88e854673120279f00660f102b`

Preserved Android debug APK SHA-256:
`6520a77bcb24f8066e4d3461bd8fdfdb6dfb296b0c00443f8577f63fe2ec0cc8`

Artifacts are under `build/development/4.0.2-dev.117/`. They preserve the prior
multimedia behavior and are NOT the corrective physical retest candidate.
