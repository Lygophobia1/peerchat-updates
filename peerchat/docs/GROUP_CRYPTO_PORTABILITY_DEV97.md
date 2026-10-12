# Shared group crypto prerequisite — dev.97

## Scope

The existing PCGE v1 group encrypted-envelope owner now delegates AES-256-GCM, secure random generation and scratch-memory wiping to a narrow platform adapter. Windows keeps the same CNG primitives. Android uses the already-pinned MbedTLS 068ff080b369adfac81509f9b57b2afabaf82dc5 dependency and compiles the same group_message_envelope.cpp into its shared native library. No dependency, signing/TLS identity, private-key export, mailbox format, group key format or server schema was added/changed.

Canonical header/AAD, signature representation, roster/key generations, exact device membership, nonce reservation, maximum plaintext, authenticated commit/dedupe and failure rules remain in the existing shared owners. No Kotlin crypto engine, group session/retry owner, same-account enrollment or historical entitlement was introduced. The current one-device-per-account policy deliberately continues to reject duplicate-account rosters.

This is a portability prerequisite, not Android group messaging UI/runtime completion. Android does not yet expose authenticated group enrollment, key-control routes or group send/receive through its application boundary. It does not yet provide group attachments or group history.

## Evidence

- The same production native envelope/security test passes with normal Windows CNG and with PEERCHAT_PORTABLE_CRYPTO selecting pinned MbedTLS AES/random/public signature verification. Windows signing remains CNG; signatures verify in the portable provider.
- Independent CNG encryption fixtures produce exactly the same ciphertext and tag for identical key, nonce, AAD and plaintext at 1, 128 and 65536 bytes. A separately authenticated CNG conflicting-ID fixture is decrypted then rejected by the production durable conflict rule. Empty plaintext roundtrip is also tested.
- Real signed three- and four-member ledgers, authoritative roster reconciliation, per-device key handoff and every member's send to each other member preserve original sender account/device, protected history and duplicate suppression.
- Existing envelope tamper, wrong-device/account, stale/future generation, missing/wrong key, nonce persistence, removal/rotation and protected-history restart regressions pass on both providers.
- Existing three-device native GroupControl key handoff/encrypted text and isolated real TLS server event-channel fanout pass. The fanout harness source list needed the existing ArchiveEnrollmentExecution.cs dependency after DiscoveryClient began referencing ArchiveRelayScope; no assertion changed.
- TransferManager bounded memory/chunks, integrity, peer binding, cancellation, disk failure, reconnect and durable restart regressions pass.
- All 120 Android JVM/Compose tests pass; debug/test/release builds and lint pass. ARM64/JNI/dependency audits and release destructive-hook isolation pass. Actual phone/Keystore group operation is not claimed.
- Full Windows build/package/import audit, group sender-attribution UI regression, accepted History Sync settings and first-enrollment UI regressions, version consistency and Development updater extraction/runtime hashes pass.
- git diff --check passes. No merge, deployment, publication or phone data changes.

The unrelated legacy Windows Android-emulation JNI harness remains an acknowledged host-fixture compile gap caused by newly shared POSIX attachment I/O. No substitute filesystem security model was introduced to make it green. This does not replace required future physical Android validation.

## Preserved artifacts

Windows: build/development/4.0.2-dev.97/DevelopmentUpdater/PeerChat-4.0.2-dev.97.zip

SHA-256: 25cd4f34099598de9ccff1ee208688e08bf89149c02782073acbe3ca2095013d

Android under build/development/4.0.2-dev.97/Android:

- PeerChat-4.0.2-dev.97-debug.apk: 5b84a6fb389976abe682724a1d0df9fe60801e4553ac8aabd827acb0da3248e3
- PeerChat-4.0.2-dev.97-debug-androidTest.apk: 35f546b39995c8f2aa24512f91d8e9139085090d9eff793cf66f46e9c3d57009
- PeerChat-4.0.2-dev.97-release-unsigned.apk: cc01fd2bfc5bd8304dd901f1de9307c4862b2d9285b12429fdfecb06687f7d07

These are Development candidates, not physical acceptance or a signed production release.

## Next authorization boundary

Explicit additional-device group enrollment must decide whether approval is limited to Owner/Admin or can be signed by an existing authorized device for another device of its own account. Account session alone is insufficient under either policy. Historical access still requires separate verified entitlement; current enrollment must not automatically expose old group history to newly joined accounts.

Independent Android History Sync enrollment parity is checkpoint 3af5ef1/dev.96. Windows-to-Windows History Sync remains physically accepted only for the host/VMTEST configuration reported by the user. Android history transfer, group UI/attachments/history, media-performance improvements and Android media remain uncompleted roadmap items, not results of this prerequisite.
