# Authenticated roster/root checkpoint — dev.106

Completes the active coherent root-evidence projection before the approved priority
switch to Android direct-message pictures/files. Production device enrollment stays
disabled. dev.105 receipts, atomic rollback and verified recovery are preserved.

The existing authenticated discovery roster now carries its bounded public signed
Create proof through DiscoveryClient/CoreAdapter into native GroupKeyManager.
The old C ABI remains available; the new evidence entry copies at most 2,048 bytes.
Both the adapter and native owner verify the actual signed Create event, immutable
legacy/server namespace, actor/subject identity and order. A cached verified root
cannot disappear or change on a later roster projection. No signature check or
current account/session-generation check is bypassed.

New group-control bootstrap may replay exactly the independently verified root
even when the original creator is no longer in the current roster. The serving
connection must still be the current authenticated distributor. This immutable
evidence grants no membership, historical entitlement or key: full signed ledger
versus current roster checks still gate current-key handoff. Legacy no-root inputs
retain the existing current-owner restriction. No enrollment RPC/UI is enabled.

Passed: portable/CNG-compatible native proof and portable server tests (tampered,
substituted, wrong namespace, downgrade and changed generation evidence); real
packaged C ABI root admission/size rejection; group envelope/control transport with
three non-friend devices, rotation and protected history; full Windows build/import
and restricted-search package audit; Android 120 JVM tests, debug/test/release and
lint; ARM64/JNI/dependency/license/release-hook audits; paired archive mutual TLS,
imports and five native worker restarts; version/updater/hash/safe extraction;
git diff --check. No new physical acceptance is claimed.

Unpublished Windows archive:
`build/development/4.0.2-dev.106/DevelopmentUpdater/PeerChat-4.0.2-dev.106.zip`

SHA-256: `0297fe1e237545ace38f650230c6e25d793dc18c87cb650d748720241959434f`

Preserved Android APK SHA-256 values under the same version's Android directory:

- Debug: `8f44de473ec634a4e63b0992081fd9cc01baf16761bc211e82518622e604ca04`
- Test: `35f546b39995c8f2aa24512f91d8e9139085090d9eff793cf66f46e9c3d57009`
- Unsigned release: `f1e9848c9d356fbf62372404fb356931b79cc4d4f8c53d15dd66e569803243f7`

No merge, publication, deployment or install. Remaining enrollment work includes
authenticated consecutive approval-receipt projection, exact application-session
binding, explicit same-account approval UI and activation/revocation coverage.
Next product work is Android pictures/files through ONE shared TransferManager,
then oversized messages/previews, before resuming group enrollment and parity.
