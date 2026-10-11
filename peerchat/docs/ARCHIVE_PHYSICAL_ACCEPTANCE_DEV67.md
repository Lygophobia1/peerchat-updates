# History/archive physical candidate — 4.0.2-dev.67

Mailbox / background Android delivery: **PHYSICALLY ACCEPTED on 4.0.2-dev.65 / d6c7153**, as reported by the user. Archive physical acceptance remains pending. No merge or publication performed.

## Candidate and unchanged deployment

- Branch: `codex/network-cleanup-phase1`. Checkpoint: the commit containing this document (use `git log -1` after checkpointing).
- Windows: `build/development/4.0.2-dev.67/Client/PeerChat.Desktop.exe`.
- Android source output: `android/app/build/outputs/apk/debug/app-debug.apk`, version 4.0.2-dev.67. A hash-identical copy is retained under `build/development/4.0.2-dev.67/Android/PeerChat-4.0.2-dev.67-debug.apk`.
- APK SHA-256: `cfbfc7bac34dbb6d38b6881c3458a0b63485cd5cac05caeec2bbb33cae0269f8`.
- Windows Development ZIP: `build/development/4.0.2-dev.67/DevelopmentUpdater/PeerChat-4.0.2-dev.67.zip`.
- ZIP SHA-256: `8e2d08126ec81d040b6b4323ddacc51d2c140cd99e5383911c363c01144dee3a`. Development unsigned policy only; no production signature or publication claimed.
- dev.66 was an intermediate validation build; it is preserved, not the physical candidate. dev.65 remains immutable.
- **Pi unchanged and running:** actual observed PID 23441, `/opt/peerchat-test/server/4.0.2-dev.46/PeerChat.Server`, config `/opt/peerchat-test/config/4.0.2-dev.46/server-config.json`, TCP `0.0.0.0:7443`; no 17443 listener. Deployment checkpoint `c1acf13` is recorded provenance from the prior deployment, not a newly queried remote Git HEAD.
- Public endpoint `173.225.14.37:7443`, LAN `192.168.1.6:7443`.
- Certificate SHA-256: `62A181282FE940F03F6A35541220A05F9ED2CB85A41F6DE3B0DF111286BA17E3`.
- Existing DB `/opt/peerchat-test/data/4.0.2-dev.33/physical-client.db` remains in use. coturn PID 2718 is unchanged. No config/DB/certificate/TURN/FCM-secret changes or restart.
- ADB found no device during this pass. No install, data clearing, identity/key/Remember Me/pin replacement occurred. Install only with `adb install -r` when the same authorized Motorola is available.

## Changes and ownership

The shared native application progress operation now starts an **initial** scan only with History Sync ON, verified fresh server metadata and source/reader grants from the same explicitly trusted authority. It cannot create grants or trust. Persisted active intent resumes through the existing Execution, SessionTransportManager and DeliveryManager; explicit pause and completed intent are not silently rescanned. Select Synchronize explicitly to scan for newer history after completion.

Android refresh, explicit consent and ON/OFF changes immediately project native progress into the existing foreground servicing path. No Kotlin history engine, permanent socket or new retry owner was introduced. Background mailbox wake remains the accepted bounded worker.

Windows archive events are tagged by native authorization and kept out of normal DM connectivity/error projection. Native authenticated account mapping supplies a local-author presentation flag for historical sibling-device messages; original sender IDs and stored bodies are unchanged. Imported history remains Sent, never a fabricated Delivered receipt. Newly imported history bypasses only the UI's one-time history-request guard, not native storage/dedupe.

Settings on both clients expose ON/OFF, signed-grant authorization (Not authorized / Authorized / Revoked), source-route availability, normalized sync state and real counters. A versioned **local application projection** extension carries verified revoked/current authorization; external protocol/server schema are unchanged. OFF suspends use but still shows retained authorization. A completed archive route retires, so it is not shown as a permanently available connection. Reopened inactive intent honestly reports paused/previously completed until explicitly selecting the saved source; active intent resumes automatically when authorized.

Historical text and attachment metadata use existing bounded paging. Windows Settings -> Archive devices -> Historical attachments -> Refresh attachments -> Download selected attachment uses the **existing TransferManager**. Its File Transfers view supplies queued/transferring/verified completion/failure/cancel state and restart recovery. Catalog presence alone is not proof that source bytes remain available; changed/missing source fails closed. An already-local verified copy is retained rather than overwritten. Android text/catalog sync is available; Android download/view controls and chat-bubble attachment redesign remain deferred, not falsely simulated.

## Automated evidence

- Native enrollment/auth/provenance, signed revocation/re-enrollment, wrong account/device, metadata freshness/tamper, OFF-preserves-grants and automatic initial scan/no-auto-consent/explicit-pause persistence passed.
- Bounded history paging, 67 imports, multi-conversation discovery, protected cursors, restart/resume, whole-page validation, live/mailbox/history dedupe and no outbox/fake ACK passed.
- Twelve real mutual Schannel archive TLS handshakes, 25 records over bounded pages, independent archive scope, unauthorized DM rejection, timeout/resume and revocation passed.
- Packaged paired Node workers, zero friends, automatic authorized initial scan, 25 historical texts (12 native-verified local-account authors), preserved sender IDs/Sent status, attachment catalog and verified Unicode 100,000-byte download through one TransferManager passed. This is HOST evidence, not phone acceptance.
- Attachment catalog tamper/binding/Unicode/duplicate names and TransferManager hash/cancel/atomic no-overwrite/process-restart/source-change/partial cleanup tests passed. Native file-session mutual TLS/opaque relay/reopen/resume/message-ACK coexistence passed.
- Accepted Go HPKE mailbox/canonical IDs/opaque store-fetch-signed ACK/dedupe/restart/quota/expiry/revocation/sender-independent fetch and push-provider stand-in passed. No claim of a new physical FCM test.
- Presence 1,000 cycles, mailbox stop/drain 100 cycles, shared reliable Schannel endpoint 102 lifecycles and protocol 1,000 cycles passed.
- Voice preference persistence and original media transport/preset/framing/NetEq/lifetime tests passed on serial recheck; an earlier concurrent transport run failed its receive-count assertion. No production voice source or assertion was changed, and that initial intermittent failure's exact cause was not proven. Diagnostic-only investigation edits were removed.
- Android: 114 JVM tests, zero failures/errors; debug/test/release APKs and lint passed (0 errors, 14 existing warnings). JNI/ARM64/system-only native imports/pinned Java modules/release fixture exclusions passed. These are host/artifact checks, not physical Keystore execution.
- Full immutable Windows build/import/package and Windows/Android version consistency passed. Development dev.65 -> dev.67 ordering, manifest/hash and production updater safe extraction passed. No update feed was published.
- WPF archive/default/OFF persistence/create-account/settings smoke and native author-presentation checks passed. Final `git diff --check` is required before committing.

## Physical checklist

Use two devices signed into the **same account**, with old history on the source. Settings -> Archive devices is the bounded testing surface. Compare full Peer IDs; do not assume sign-in gives archive access. Windows can be the source or reader; Android supports text/catalog testing. Preserve all existing profiles/keys. Use a separately created test profile only if an unauthorized third device is needed; do not rotate an existing identity.

1. Refresh devices on both; explicitly trust the chosen authorizer. On that authorizer grant source **May serve** and reader **May receive**; publish/refresh. Keep History Sync ON.
2. Before granting another same-account device, confirm it cannot sync; signing in alone must not grant access.
3. Confirm initial old-text scan starts on the authorized reader and reaches Complete. Use Synchronize from selected archive for an explicit later scan.
4. Check received and sent messages retain original attribution, including messages authored by the source's other account device.
5. Check logical messages appear once; repeat a scan and compare counts/history.
6. Interrupt a multi-page scan by closing the reader normally; reopen without deleting any data.
7. Confirm active scan resumes from the protected cursor, with no resend outbox/fabricated ACKs.
8. Send a live message while archive scan is running (and, where practical, deliver a mailbox message).
9. Verify convergence yields one copy per logical message ID in the correct conversation/order.
10. Set History Sync OFF; verify local history, authorization and cursors remain, with archive work suspended and ordinary messaging/mailbox working.
11. Set ON; confirm previously active scan resumes. Explicitly paused scans remain paused until Synchronize is selected.
12. Revoke the reader's archive access using the existing authorizer and refresh both clients.
13. Confirm future archive pages/restore are denied, with authorization Revoked; already imported local history remains readable.
14. Reauthorize with a newer signed grant, refresh and explicitly synchronize; no old revoked scope may resume silently.
15. On Windows reader, restore a small and a larger historical attachment; verify bytes/hash and original filename without overwriting unrelated files.
16. Interrupt attachment transfer and restart source/reader; verify existing TransferManager resume/cancel/failure behavior, not a fake completion.
17. Restart both apps; confirm identities, trusted server pin, Remember Me, text/history/attachment state and saved OFF/ON persist. Idle archive routes must retire.

Record failed item and safe operation/stage diagnostics. Do not weaken trust or redeploy/erase the server to make a test pass. Stop for the user's physical results.
