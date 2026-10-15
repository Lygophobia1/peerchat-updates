# Android dev.161 — scoped mailbox recovery candidate

Date: 2026-10-15. Physical acceptance pending. No phone installation, real binding migration, Pi mutation or Main Work modification was performed.

## Repository and package correspondence

Worktree: `C:/Users/Livel/Documents/Codex/peerchat-android-groups-voice`; project `peerchat`; branch `codex/android-groups-voice`.
Starting HEAD: `467111c5f20a66859503f1de4bf99d0332a20c6e`.
Native checkpoint: `8cfd31d` (scoped enrollment and offline regressions).
Compiled source checkpoint: `dba61c093beaffb217ef79bafe29eb4384955289` (Android/JNI integration).
The final documentation HEAD and clean status are recorded in the delivered checkpoint manifest; later documentation changes do not change this APK's source.

APK: `PeerChat-4.0.2-dev.161-scoped-mailbox-reenrollment-debug.apk`, app `com.peerchat.android`, versionCode 2, ARM64, min SDK 26, target 35.
SHA256: `33d2b38be76ec7d0cfe8de075ff50920bc318255c7e07bdbb47d789e9b9c7443`.
Signing certificate SHA256: `db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`, matching prior diagnostic APKs. This is a replacement-installable debug candidate; no final release APK was built. Previous packages remain preserved.

## Demonstrated cause and server evidence

The retained original signed recipient binding is for account 19. The authenticated Linux account is 4. The existing device identity, public key, key identifier and generation match the original signed binding; the opaque Android Keystore key is accessible. Native KeyCoordinator correctly rejects the account mismatch at `binding_account` within `binding_read_validate`. Registration therefore never completes and fetch is not requested. This is retained account authority after account recreation, not evidence of a missing private key or a Sammy verification defect. Accepting Sammy's fingerprint does not rewrite local mailbox authority.

Motorola identity: `61f70fb01a6055499eed089025e8e8e2850d640b4c4a6ba3ad772168f092b51b`.
Original material ID: `f523b9d70c6a40fab95d4b379d5d3716`, generation 2.
Prior authorized read-only server metadata confirmed account 4 JonahPhone owns this device. At inspection there were no recipient registrations or pending ciphertext for the Motorola. The five pending messages belonged to Windows device `35049f3bf26f1d01a7e6c2d0dad2132a646093008d0c2f23f96fa32aa418201b`, not this phone. No pending message is redirected. This is a dated snapshot, not a claim about future server state.

The user physically confirmed live Android/Windows messaging in both directions and the existing Sammy verification prompt. Those working paths are retained.

## Implemented recovery and security boundaries

The user approved implementation, with a separate mandatory on-phone confirmation. No automatic account rebinding occurs.

1. Configure scope from the current authenticated native account/generation, actual server endpoint and pinned certificate. Present the original account, new account, phone fingerprint, source enrollment and server fingerprint.
2. Keep existing binding / dismissal writes no new enrollment and preserves all source authority and queued records. No automatic approval occurs. The prompt may be offered again after restart or a new authenticated session.
3. Re-enroll mailbox first refreshes authenticated pinned bootstrap/device ownership, then revalidates the same account, session, device and exact current offer. The bounded offer includes a fresh nonce and expires after five minutes; stale offers cannot approve renewed sessions.
4. Native code verifies the original identity signature and immutable source metadata, opens the existing opaque key under its existing registry lease, and proves possession with the existing HPKE provider and a scope-bound random challenge. Any failed check stops with a fixed actionable error.
5. Create a new identity-signed account-4 enrollment ID, first target generation 1 and empty predecessor, using the SAME original agreement key. Source material remains generation 2. Preserve the original signed account-19 binding byte-for-byte. Persist exact Pending bytes in a supplemental protected record in the existing registry database.
6. Use the unchanged authenticated server recipient-registration protocol. Only exact server-accepted binding confirmation marks local authority Accepted; fetch/decrypt/import/ACK then use this accepted scope. A server rejection preserves Pending and ciphertext and reports an error, rather than replacing keys or silently retrying with different enrollment IDs.
7. Enforce current account, server scope, device, source lease and signed binding before decrypting. Account-19 wire cannot be imported as account 4. Existing durable import, duplicate suppression and signed ACK semantics remain intact. History commits before ACK.

The supplemental `mailbox_scoped_enrollment` table stores a protected version-1 authority record, with Pending/Accepted/Revoked states, original binding digest and exact target binding. Original PKR1 data and Keystore alias are unchanged. Local scoped revocation blocks only that enrollment and cannot be resurrected by approval; physical source-key revocation blocks all scopes. No second trust database, wire-format change, C ABI break or server upgrade is introduced. Older APKs retain the original registry but cannot use the new scoped enrollment; downgrading does not provide offline recovery.

**Candidate limitation:** turning mailbox storage OFF for a recovered enrollment is blocked before saving the preference, with a message that consent remains ON. A safe authenticated consent transition needs separate work; the existing server revocation path can purge undelivered ciphertext, and this repair must not invoke it or rotate the shared key. Pending, revoked, unavailable and offline cases fail closed rather than falsely reporting OFF.

## Modified components and integration manifest

- Shared native: new `mailbox_scoped_enrollment.hpp`; `delivery_manager.hpp` adds scoped receive while sharing the unchanged import/receipt body. Existing receive entry point remains available.
- `portable/peerchat_core.cpp`: additive enrollment JNI exports, current-scope configuration, exact offer/decision validation, accepted registration and account-gated receive. Package Kotlin and JNI together.
- Android: `MailboxBridge.kt`, application DTO/parser and backend, snapshots, presenter, Compose confirmation and actionable errors. Existing peer verification/outgoing paths retained.
- Tests: native scoped security/offline fixtures and runners, Kotlin contract/parser/presenter/UI/consent tests, isolated JNI/Keystore instrumentation fixture.

Read-only Main Work manifest reviewed at `088f34229a89193186c331d9ead7a1fe2d6bd123`, `docs/TRANSFER_CACHE_INTEGRATION_MANIFEST.md`: Windows dev.160 owns local ui_mailbox operation 9 and attachment-cache recovery. It changes no Android/JNI or mailbox wire protocol. No TransferManager, group crypto, attachment envelope, Pi cache or media change is duplicated here. Integrate the native header/DeliveryManager checkpoint first, then the matching JNI/Android checkpoint; resolve shared core/header hunks manually and rerun both mailbox and attachment regressions. Keep Main's attachment gating, receipt authority and Windows behavior unchanged. Android group text/voice WIP remains preserved; this milestone adds no group attachment or voice functionality.

## Validation and limits

- Final Gradle run: 219 Android host tests, zero failures/errors/skips. Lint: zero errors/fatal issues, 25 existing warnings. Debug ARM64 APK and instrumentation APK built successfully.
- Scoped native security regression: approval/denial, failed possession, immutable source preservation, Pending/Accepted restart, wrong scope/account/old wire, corrupt record, scoped and physical revocation, durable import and duplicate signed receipt passed.
- Scoped offline regression passed with Windows BCrypt and Android MbedTLS verification modes: account-19 source to explicit account-4 same-key enrollment; ciphertext stored; sender fully closes; complete recipient profile/registry/enrollment reopens; transient fetch failure; durable import; lost ACK; duplicate refetch returns exact receipt; sender restart reconciles once. Every reopen checks original protected source bytes and alias unchanged. Its transport/auth layer uses an immutable ciphertext spool.
- Separate existing real pinned-TLS server suite passed identity-bound registration, offline store, sender logout, recipient reconnect/re-registration, connection failure retention, fetch/ACK, authorization/tamper/quota/rotation/revocation and receipt persistence. Original native reconnect fixture also passed after the shared receive refactor.
- Instrumentation fixture compiled with real JNI/Keystore interfaces but was NOT executed on ART, emulator or phone. No combined Android-app-to-live-Pi recovery acceptance has been performed.
- Initial NDK compilation found a local RAII/template linkage problem. Moving wipe guards to class scope fixed it; final build passed without warning suppression or toolchain replacement. Failed and successful logs are retained.

Evidence: `build/tests/android-session/dev161-package-final.log`, `dev161-scoped-enrollment.log`, `dev161-scoped-offline-recorded.log`, `dev161-mailbox-protocol.log`, Gradle JUnit XML and lint report; copies accompany the deliverable.

## Physical acceptance — requires the owner

1. Install this APK over the existing app. Do not uninstall or clear data. Confirm account 4 is authenticated and the permanent identity, history and trusted Sammy fingerprint are unchanged.
2. Review the account-19 to account-4 confirmation, server fingerprint and phone fingerprint. Keep existing binding/dismissal must leave recovery unperformed. Only tap **Re-enroll mailbox** when ready to authorize it on this phone.
3. Wait for native registration to complete (`scoped_binding_prepared_local` followed by registered=true). If approval, key access or registration fails, retain data and return diagnostics; do not reset or retry through identity replacement.
4. Confirm live messaging still works both directions.
5. Close/disconnect Android. Send a NEW uniquely labeled Windows message and confirm Stored for delivery for the current Motorola device. Fully close the Windows sender. Reopen/reconnect Android. Confirm exactly one visible message, history persistence after restart and delivery acknowledgment when Windows returns.
6. Repeat after Android restart and a transient network interruption; check no duplicates and no loss. This validates fetch, decrypt, durable import, UI projection and ACK rather than presence alone.

No known Motorola ciphertext was pending at the prior server inspection. Messages for Windows or another device are not recovery inputs. Previously stored messages are recoverable only if actually addressed to this device and its accepted enrollment key; no routing or account-isolation exception is added.

## Remaining work and resume boundary

Physical same-key possession/enrollment and the full Android offline sequence are the next essential checkpoints. Stop here for owner testing. Investigate only any exact failed stage returned; do not change working live messaging or verification. After physical acceptance, separately design authenticated storage-consent OFF behavior that preserves pending messages. Group/voice and optional UI enhancements remain outside this repair.
