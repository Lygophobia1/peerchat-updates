# Signed group-device server boundary — dev.100 prerequisite

This checkpoint is a gated prerequisite, not complete production enrollment or physical acceptance. No live server deployment, device enrollment, identity replacement or historical access change is performed.

## Proof and ownership

The shared signing-only builder uses the existing Profile provider. It never requests or serializes a private key. The target signs `PeerChat/group-device-identity/v1` with its account, exact device identity and public key. This public possession proof is not a bearer login credential. The current approver separately signs `PeerChat/group-device-approval/v1` containing the server/legacy group identifiers, exact expected roster generation, same account, both public identities, target proof and existing signed AuthorizeDevice/RevokeDevice ledger event. Server verification checks both signatures and the inner event ID/signature/purpose.

The database then checks the authenticated account/device, authoritative device ownership, current approver group membership, exact legacy group binding and exact membership epoch. An ordinary member may approve its own account's device; account login alone cannot do so. Approval inherits that account's current role and is limited to eight devices. Device revocation cannot remove the last member device. Account leave/removal clears all of its enrolled devices. Rejoining does not restore an old sibling enrollment.

Successful changes and public signed receipts commit in one SQLite transaction. An exact duplicate is idempotent only at its applied generation; after any later mutation, replay fails closed. Reopening the database preserves both authorization and receipts. No key material, message body or historical grant is stored in these proof records.

## Activation boundary

The server method is not exposed by an RPC yet. Production GroupLedger enrollment remains disabled. Signed replica reconciliation, authenticated target-proof publication, exact multi-device key provisioning and application confirmation must be completed and tested before activation. The helper is compiled by Android's shared native core but is not exposed as an enrollment JNI operation. Existing transport/delivery/key ownership is unchanged.

## Automated evidence

`tests/test-group-device-enrollment.ps1` creates fresh isolated test profiles and tests actual native serialization against the Windows server verifier/database, with CNG and portable signing builds. It covers authenticated account/device mismatches, substituted server group, missing target authorization, invalid target proof with a valid outer signature, wrong account in target possession proof, damaged inner event with a valid outer signature, tampering/truncation, stale epochs, durable duplicate receipts, revoke/re-enroll, ordinary-member same-account approval, account leave/rejoin and all-device account removal. Existing three-client real-TLS opaque group fanout also passed. Build/package and additional regression results are recorded in the continuation ledger after completion.

The expanded test also passes the eight-device bound, competing same-epoch approval race (exactly one wins), missing server registration and original-device revocation with surviving account ownership. Full Windows dev.100 build/import/runtime-only package audit passed. All 120 Android JVM/Compose tests, debug/test/unsigned-release builds and lint passed; ARM64/JNI/native dependency, pinned TLS modules and release-hook isolation audits passed. Source, generated Windows constants, Android versions and dev.99-to-dev.100 updater extraction/runtime hashes agree. Packaged native registration, five archive worker restarts, History Settings and real WPF first-enrollment handler regressions passed. The registration harness needed its existing ArchiveEnrollmentExecution source added to the compiler list; this is test-only dependency repair, not a production change. Existing compiler/deprecation warnings remain; no physical execution or release signature is claimed.

Preserved unpublished package: `build/development/4.0.2-dev.100/DevelopmentUpdater/PeerChat-4.0.2-dev.100.zip`, SHA-256 `58a744fcbd96ab37da8deb5f9999dbe3ab5a03ebcbb8608cd58ef7693c7df6b4`.

Android files are preserved under `build/development/4.0.2-dev.100/Android`: debug `d663800af862fd3abfd9f2d053315d678f1cec9367f61cc19fe737a1bac14fad`, instrumentation `35f546b39995c8f2aa24512f91d8e9139085090d9eff793cf66f46e9c3d57009`, unsigned release `69bc58444d77b9ab8fedb86c0d5191f08d87267806962382712944a567604133`. Artifacts are ignored build evidence, not committed binaries.
