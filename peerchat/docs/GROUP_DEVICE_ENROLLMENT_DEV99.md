# Signed group-device ledger prerequisite (dev.99)

Approved policy: an already-authorized group device may explicitly approve another
device of its own account. Account login alone does not confer group membership.

This checkpoint adds the shared native ledger boundary only. AuthorizeDevice (10)
and RevokeDevice (11) are signed GroupEvent extensions, using the existing device
identity and canonical signed event framing. Existing event kinds and signatures
are unchanged. Approval binds group, actor/subject account and device, target public
key, logical order and the `PeerChat/group-device-authorization/v1` purpose.
Revocation has a separate purpose. No TLS/signing/encryption identity is created.

Logical account membership and roles remain separate from explicitly authorized
additional devices. Only a currently authorized device may approve its own account's
device. Substituted keys, cross-account approvals and revoked issuers fail closed.
At most eight devices per account are admitted. Account removal clears additional
devices; role changes propagate without independently elevating a device. Revoking
the original device deterministically retains an approved sibling and the account
role. Revoking the last device is rejected rather than silently deleting membership.

No public application/JNI/server enrollment command is enabled in this prerequisite.
The default production ledger rejects both new event kinds; only focused fixtures
explicitly enable the prerequisite. Server integration must be completed before
enabling it for authenticated application replicas.
The existing server roster still rejects duplicate-account device entries. Key use
also fails closed when a signed ledger contains additional devices absent from the
server roster. Therefore this checkpoint does not yet provide end-user multi-device
group enrollment. Follow-up must verify authoritative account/device registration,
bind the explicit proof to the current server roster epoch, persist the proof,
reconcile it into every signed replica, rotate current key distribution and support
device-specific revocation before exposing the high-level approval action.

Approval does not issue history grants, distribute historical group keys, modify
friend trust or confer historical access. Existing Windows History Sync acceptance
is preserved. Native CNG and pinned portable MbedTLS test paths use actual signed
events, with tamper/domain/cross-account/revocation/replay/reverse-arrival and
protected-storage reopen checks. Host tests are not physical multi-device acceptance.

This is an unpublished development prerequisite, not group attachment/Android group
UI/media completion. Existing identities and unrelated dirty work remain untouched.

## Automated validation and preserved evidence

- Final CNG and portable MbedTLS envelope tests pass, including ordinary-member
  approval and production-default rejection. Existing three-/four-member encrypted
  fanout, provenance, durable dedupe and rotation cases pass.
- Existing eight-member ledger/roles/removal/ACK/encrypted-restart tests pass.
- Native GroupControl scope, non-friend mutual-TLS key handoff and three-device
  encrypted text/reload/pagination/removal tests pass. These remain one-device-per-
  account transport tests, not completed multi-device server integration.
- History enrollment/storage/session-scope/server-metadata/mutual-TLS tests pass;
  packaged paired Node archive transfer, five worker restarts, Settings and real WPF
  first-enrollment handlers pass. No physical acceptance is inferred from fixtures.
- Android: 120 JVM/Compose tests, debug/test/release builds and lint pass. Final
  ARM64/JNI/dependency/license/release test-hook audits pass. The known Windows
  Android-emulation POSIX attachment harness gap was not repaired or claimed green.
- Full Windows build, runtime import/package audits, matching Windows/Android
  versions and dev.97-to-dev.99 updater extraction/runtime hashes pass.
- git diff --check passes. Existing compiler warnings were not broadened into
  unrelated cleanup. No phone install, server deployment or publication occurred.

The immutable builder reserved dev.98 for the earlier intermediate build; the
final fail-closed gate was rebuilt as dev.99. Only dev.99 is this checkpoint's
candidate. Scratch dev.98 outputs are retained, not published or accepted.

Windows: build/development/4.0.2-dev.99/DevelopmentUpdater/PeerChat-4.0.2-dev.99.zip

SHA-256: dafa8b2cf5905400dfcc0d87ce7ba6b44376472aec50816457b298ad8e27921d

Android artifacts are preserved under build/development/4.0.2-dev.99/Android:

- Debug APK: 7fd1f2d9db9494429b55d5190c3362a4975bd91ce3b6a642638f6f85ccd1b1d1
- Test APK: 35f546b39995c8f2aa24512f91d8e9139085090d9eff793cf66f46e9c3d57009
- Unsigned release APK: bd949bf5bb1b2895cd6d108f0d0f57d172e2ac04241ce685efcadb3748f7caf4

Development manifests are unsigned under the existing development-only policy;
no release-signature or production-publication claim is made.
