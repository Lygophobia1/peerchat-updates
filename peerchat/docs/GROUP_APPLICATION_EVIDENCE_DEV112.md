# Authenticated group evidence projection — dev.112

The authenticated roster now carries signed device approval/revocation evidence
through the managed application model and a copied, bounded native C ABI. Native
code independently verifies the immutable signed creation root, signatures,
namespaces, applied generations, ordering and event uniqueness before queuing a
roster. The existing account/discovery-generation admission remains authoritative.
Conflicting proof bytes at the same roster generation fail closed. Bulk roster
listings intentionally omit proofs and do not erase previously verified evidence
at an unchanged generation.

Server evidence is a newest-first selection returned in ascending order, with at
most 32 entries and an 8 KiB JSON budget. `deviceApprovalsIncluded` distinguishes
bulk omission; `deviceApprovalsTruncated` explicitly reports a partial window.
Forty real signed approval/revocation transitions verify this bound, newest-proof
retention, ordering and compatibility with the legacy small-response limit. This
budget bounds added evidence, not all pre-existing large roster/list payloads.

Production additional-device enrollment remains disabled. Public evidence is NOT
a complete ledger, an admitted event, a protected replica receipt or a key grant.
Missing intermediate epochs require verified reconciliation; no truncated window
may substitute for it. Exact issuing-session admission, application replica
integration and explicit approval/revocation UI remain outstanding before enabling
enrollment. Existing durable receipts, rollback protection and restart verification
are preserved. No historical access is automatically granted.

Android DM pictures/files, protected typed-text previews and recent durable
attachment presentation from dev.107–109 remain intact. Physical validation of
those changes is pending; no installation, deployment, merge or publication was
performed.

Validation passed:

- Windows CNG and portable native signatures, proof projection, malformed bounds,
  tampering, wrong accounts/namespaces/epochs, durable replica recovery and default
  enrollment rejection.
- Windows and portable server approval/revocation, concurrency, restart, removal,
  explicit reenrollment and the bounded 40-transition evidence window.
- Managed projection and actual packaged native creation/proof C ABI contracts.
- Three-device group encrypted text/key handoff, duplicate prevention, protected
  history reload/pagination, non-friend control scope and removal key rotation.
- 127 Android JVM/Compose tests; debug/test/unsigned-release builds and lint;
  ARM64/JNI/dependency/license and release-test-hook audits.
- Full Windows build and package/import audit; version consistency and Development
  updater extraction/runtime hash audit; git diff --check.

dev.111 was a local intermediate package candidate. dev.112 adds the explicit
server evidence byte budget; neither artifact was overwritten or published.

Windows package:
`build/development/4.0.2-dev.112/DevelopmentUpdater/PeerChat-4.0.2-dev.112.zip`

SHA-256: `5334499e4f4b664ee39fe216d1498439021ce19600ac2d0ff9223d7d2d5bb4b3`

Android packages: `build/development/4.0.2-dev.112/Android/`

- Debug: `2c328551a36f6c91f6ed008bbfff03fb28b94314c630e4ce0201429e45aac99a`
- Test: `e4fcac9fa788d8fe682b0a5cad85fbdbE66c6cd95aa9b356512033d5b6c9a8c5`
- Unsigned release: `b340a30295ed64c2aa8c7a22f02a203d91a5425fb75ad70cbe96ccef8ced6203`

These are automated test results, not physical group enrollment/media acceptance.
