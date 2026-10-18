# Persistent device display names

## Scope and authority

Follow-up to dev.179 on `codex/native-unicode-emoji`, starting at `fe1a601` in `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`.

The user requested consistent names in Device Manager, History Sync and message recipient details, then explicitly approved sharing device names with authorized contacts. Names are cosmetic labels, not verified hardware descriptions, fingerprints, account ownership, delivery eligibility or authorization decisions.

## Confirmed mismatch

- Device Manager stored names in the server's existing account/device authority records.
- Windows History Sync used a separate protected local nickname file.
- Message receipt rows displayed shortened fingerprints without device names.
- Android already had some own-device labels, but not a shared contact-name projection or consistent History Sync detail labels.

## Implementation boundary

The existing server rename operation remains authoritative. Updated clients opt into `friend_state` with integer `deviceNamesVersion=1`. Accepted, non-blocked contacts receive an optional `deviceNames` dictionary keyed by exact device fingerprint. Public search, suggestions, pending requests and blocked contacts do not receive names. The map contains no credentials, keys, private inventory, platform or app-version metadata. Historical/revoked labels may remain available to identify old receipt rows; they never restore authorization.

Legacy requests, bootstrap responses and event snapshots retain their existing shape and bounds. Negotiated friend-state responses are bounded to 64 KiB. Optional metadata failures can fall back to the legacy identity/revocation projection without erasing cached labels. Rename uses the existing contact-change hint; it does not tear down a route or create a new authentication generation. No message, transfer, ACK, History Sync record or cryptographic format changes are required.

Windows retains display labels in protected profile-local storage, uses a common resolver for history and receipt rows, and directs History Sync rename actions through Device Manager's existing checked rename operation. The old nickname file remains a fallback, not an automatically uploaded migration. Android uses its existing protected friend index and own-device inventory. Missing labels fall back to device fingerprints. Full identity remains accessible and duplicate names do not merge devices.

Previously disclosed labels cannot be remotely erased from another device or an old receipt. Removing/blocking a contact prevents further sharing; cached historical names do not confer ongoing access.

## Validation and checkpoints

Completed source checkpoints: `46b142b` (opt-in server sharing) and `2cb3f0e` (Windows/Android integration, portable bound correction and dev.180 package source). The documentation-only checkpoint follows these. No live device was renamed or revoked for testing.

| Validation | Result / evidence under `build/` |
| --- | --- |
| Windows isolated server | PASS negotiated sharing, Unicode, search/pending/removal/block privacy, unchanged legacy framing, corrupted optional labels, revocation, restart and queued ciphertext; `dev180-device-management-server.log` |
| Windows UI and models | PASS 62 assertions, including actual dev.180 package; `dev180-packaged-ui.log` |
| Real pinned TLS managed adapter/server | PASS 57 assertions; rename, contact label sharing, protected restart, retained revoked labels; `device-manager-adapter-names.log` |
| Native publication and receipt projection | PASS label-only refresh leaves authentication mapping and receipt state unchanged; `device-names-delivery-projection.log` and agent-run authenticated-friend-publication fixture |
| Android | PASS 398 tests, 89 suites, zero failures/errors; lint and ARM64 APK assembly; `dev180-android-build.log` |
| Portable server suite | PASS shared JSON, signatures, device management, presence, groups, durable mailbox, pinned TLS reconnect and lifecycle tests; `dev180-server-portability-verified.log` |
| Full Windows and Linux builds | PASS; `dev180-windows-build.log`, `dev180-linux-build-verified.log` |
| Immutable package audit | PASS source/version, original Android signer, ARM64 ABI, native dependencies and all ZIP payload bytes; `dev180-package-audit.log` |

Validation found and corrected two integration issues: standalone test projects needed the new cosmetic helper, and the portable JSON serializer throws `ArgumentException` where Framework throws `InvalidOperationException` for the same response bound. Both now retain bounded, nonfatal optional-metadata behavior. Earlier logs and the unshipped Linux intermediate are preserved under `build/`; no accepted artifact was overwritten.

Names remain server-persistent; Android own-account inventory is refreshed after authentication and is not treated as cached authority while offline. Contact display labels are retained in encrypted local storage. A legacy server has no shared labels, so clients keep known labels or fingerprint fallback. No claim of instantaneous propagation while disconnected is made.

## Packages and approved Device Manager server update

Paths relative to the worktree above:

- Windows: `build/development/4.0.2-dev.180/PeerChat-Windows-4.0.2-dev.180.zip`; SHA-256 `eb0de3ac7032a49383181268649b61f7bdcebf9a8f8eb55633187bece18d448e`.
- Android: `build/development/4.0.2-dev.180/Android/PeerChat-Android-4.0.2-dev.180-debug.apk`; SHA-256 `49b98a29339b74c7b9f02fdc14e758af3ff87e6bc727f9ea55dc572dca7f4cf5`.
- Pi ARM32: `build/linux/4.0.2-dev.180/PeerChat.Server-4.0.2-dev.180-linux-arm.zip`; SHA-256 `f0565ca3a84e6527ade6dfaca6ef71d2b957424dc43cdfb473fc8313ed60267f`.

The existing Device Manager deployment approval was used for this display-name follow-up only. Pi dev.179 was updated to dev.180 with `pi-compatible-update.py`; backup: `/opt/peerchat-test/rollback/pre-4.0.2-dev.180-20261018T013918Z`. Pinned TLS, existing test authentication, discovery, unchanged protected identity/grant content, unchanged certificate/configuration and cache reserve/filesystem checks passed. The cache remains enabled. Running PID 29236 and version were verified read-only. Composite startup health took 6,482 ms with zero retries; this is not a message latency measurement. Evidence: `dev180-pi-deployment.log`.

Previous packages and coherent database/cache backups remain available. Rollback must preserve current database/cache and revocation authority; never restore stale user data merely to roll back binaries. No phone installation or physical acceptance is claimed. Android voice changes require separate Pi deployment authorization.

## Physical acceptance

1. Rename a known device in Windows Device Manager, checking its full fingerprint first.
2. Confirm the same label in own-account History Sync and an authorized contact's message receipt details after refresh/reconnect.
3. Restart both clients and verify labels persist; confirm two devices with identical names remain separate.
4. Send a message and confirm Received, Pending and Unavailable remain accurate per device. A rename must not change those states.
5. Verify ordinary messaging, direct/cached attachments and History Sync still work. Do not revoke a valuable device just to test a label.

Android voice is separately authorized as the next milestone only after this work is checkpointed. No voice implementation or phone installation is part of this change.
