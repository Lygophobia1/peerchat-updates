# Group and Android continuation

## Physical evidence accepted

The user reports successful Windows host to VMTEST History Sync on a newly created account: explicit first-device setup, device authorization/approval and actual missing-history transfer. Windows-to-Windows History Sync is physically accepted for that tested configuration. Preserve the compact interface, native authorization, identities, grants, custom settings, protected history and cursors. This does not establish Windows-to-Android or group-history acceptance, or acceptance of every historical profile mapping.

## dev.96: Android enrollment parity prerequisite

Android consumes the opt-in v2 local native approval snapshot already used by Windows. Normal controls expose first-device Set up, eligible Authorize, and explicit approval of an existing registered issuer. They compose the same scoped consent/signing/publication operations; rendering/sign-in never grants access. Current authenticated account/generation, native eligibility and exact server-current locally owned permissions are checked before claiming success. Automatic synchronization remains native policy, not a Kotlin retry/transfer owner. Existing custom permissions and disabled authorizers remain intact.

New JVM adapter tests cover v2 parsing, first-use confirmation/cancellation, retained authority rejection and exact issuer selection without grant issuance. Compose tests cover visible primary actions, cancellation, ON/ON new-device defaults, state transition and ineligible guidance. Existing native enrollment and TLS/import tests cover the actual signature/admission boundary; the new JVM serialization fixture is not a phone/Keystore proof.

Physical Android enrollment and Windows-to-Android transfer remain pending. No app data, signing/encryption key, remembered authorization or local history was cleared. No phone install, deployment, merge or publication is part of this checkpoint.

Validation: all 120 Android JVM/Compose tests passed, debug/test/release APKs and lint passed; native enrollment/history/session-scope/server-metadata/mutual-TLS regressions passed; full Windows dev.96 build/import/package audit passed; packaged native archive application boundary, debug/release ARM64 archive JNI exports, profile/TLS dependency and release test-hook isolation passed; version/updater dev.95 to dev.96 extraction and runtime hashes passed. The pre-existing Windows Android-emulation JNI harness failed compilation at the POSIX attachment provider (first error missing mbedtls/sha256.h); it was not weakened or claimed passing. Real Android compilation and static JNI audit are not physical JNI execution.

Windows package: build/development/4.0.2-dev.96/DevelopmentUpdater/PeerChat-4.0.2-dev.96.zip. SHA-256: c2ca125c43703b34839558223af1d528c486f52477a55249ad71ea36d84a14e9. Android artifacts: android/app/build/outputs/apk/{debug,androidTest/debug,release}. Version 4.0.2-dev.96; release APK is unsigned and no publication is performed.

## Audited Windows group gaps

- Group text has an authenticated encrypted event-channel path, protected history, real sender-account attribution and durable dedupe.
- Chat attachments and bounded drag/drop currently reject groups. TransferManager is one existing reliable-route engine with a four-active-transfer bound; group fanout must preserve that bound rather than instantiate another engine or eagerly open every contact route.
- GroupKeyManager and signed GroupLedger currently bind one device per account. Account equality is not additional-device authorization.
- The server roster, key-distributor reconciliation, encrypted-envelope sender verification and signed ledger must agree on an explicit additional-device proof before any multi-device enrollment can be enabled.
- Historical group membership/key generations are not currently published as a sufficient provenance proof. Current membership or History Sync approval cannot authorize old group records for a newly joined account.
- Android compiles shared signed group ledger/storage but does not yet expose the Windows group envelope/key/session owner through JNI/application state. Group AEAD/key generation still directly use Windows CNG; a narrow platform crypto adapter is an independent portability prerequisite, not a new Kotlin encryption engine.

Approved additional-device policy: an existing authorized group device may explicitly approve another device of its own account. Account login remains insufficient. The dev.99 signed-ledger prerequisite is documented in GROUP_DEVICE_ENROLLMENT_DEV99.md; server proof/epoch and application integration remain follow-up work, not completed enrollment.

## Remaining sequence

dev.100 adds the native signing-only double-proof builder and authoritative server SQLite enrollment/revocation boundary. Production enrollment remains disabled; no RPC/UI claims success. CNG/portable adversarial proofs, exact-epoch race, bounds, durable receipts, leave/rejoin, existing group fanout/key/control, History Sync security/import/TLS, packaged registration/application/WPF, 120 Android tests/builds/lint, ARM64/JNI and full Windows/updater/version audits passed. See GROUP_DEVICE_SERVER_PROOF_DEV100.md for scope, preserved package hashes and activation prerequisites. Next work is exact enrolled-device key/message provisioning and signed replica reconciliation, not another account-identity or retry owner.

1. Secure multi-device proof + roster integration, independent revocation/epochs, then Windows group attachments using the existing transfer pipeline with truthful per-recipient state.
2. Group-history catch-up only with independently verified historical entitlement and original provenance.
3. Shared group crypto/runtime portability and Android group application/UI integration.
4. Shared transfer Android adapters and bounded preview/cache parity.
5. Cross-platform history enrollment/transfer/restart/live-refresh acceptance.
6. Evidence-driven Windows media measurements (Native Windows crash stays deferred), then bounded Android voice/view/share stages with platform permissions.

Every item needs its own functional/security tests and coherent checkpoint. Three-/four-member host cases do not claim physical group or TURN acceptance. Development artifacts remain unpublished.
