# PeerChat dev.59 physical acceptance

Candidate: 4.0.2-dev.59. Physical results are **pending**, not inferred from host tests.
Use the checkpoint containing this document; the final report supplies its hash.
No merge, publication, production release or automatic archive authorization occurred.

## Artifacts and server

- Windows: `build/development/4.0.2-dev.59/Client/PeerChat.Desktop.exe`
- Android replacement install: `adb install -r build/android/4.0.2-dev.59/debug/app-debug.apk`
- Android tests: `build/android/4.0.2-dev.59/test/app-debug-androidTest.apk`
- Unsigned release artifact: `build/android/4.0.2-dev.59/release/app-release-unsigned.apk`
- Development updater: `build/development/4.0.2-dev.59/DevelopmentUpdater/PeerChat-4.0.2-dev.59.zip`
- Updater SHA-256: `36ef0e9d7492dc9339bffea2a8a37cbc87a99a2185474ef9e8d802ebd04abd54`
- Pi unchanged: dev.46, deployment checkpoint c1acf13, observed PID 23441,
  `/opt/peerchat-test/server/4.0.2-dev.46`, TCP 7443.
- Config: `/opt/peerchat-test/config/4.0.2-dev.46/server-config.json`.
- DB: `/opt/peerchat-test/data/4.0.2-dev.33/physical-client.db`.
- Public endpoint: `173.225.14.37:7443`; LAN: `192.168.1.6:7443`.
- Live TLS pin: `62A181282FE940F03F6A35541220A05F9ED2CB85A41F6DE3B0DF111286BA17E3`.
- Coturn PID 2718 was observed unchanged. No server/config/data deployment was needed.

## Preparation and boundaries

Use existing profiles/identities; do not uninstall, clear data, regenerate keys or
copy a profile to manufacture another device. For archive checks use two legitimate
devices on the **same account**: A (Windows source with history) and B (Windows
reader or Motorola reader). Use C, a different authorized contact account, for
ordinary messaging/voice/file tests. Two Windows profiles can exercise the archive
file UI; actual cross-PC and Motorola checks remain distinct evidence.

Before upgrading record each device Peer ID, recipient-key generation and server
endpoint/pin from existing safe diagnostics. Retain backups with the normal backup
UI if desired. Never put secrets or message contents in exported test diagnostics.
Do not touch your sole history/file copy for error tests; use disposable test files.

Archive files require completed TransferManager records retained by this candidate
and authenticated device/account evidence. Send fresh test files after upgrade for
K. Unknown/unverifiable historical device ownership fails closed rather than being
invented. Android text sync is testable; Android attachment download UI is deferred.
Native Android catalog/download/cancel and file-provider instrumentation are built,
but this run had no connected ADB device and claims no physical provider result.

For every item record PASS/FAIL and notes; leave unperformed checks pending.

## A. Login / account creation / trust

1. **Start:** existing Windows and Motorola profiles, remembered authorization enabled.
   **Action:** replacement-install Android, launch both clients and inspect About/build;
   close and cold-launch again without signing out.
   **Expected:** dev.59; same device/recipient identities and history; saved endpoint
   and pin survive; remembered sign-in reaches Online only after real presence.
   - [ ] PASS  - [ ] FAIL — Notes: ____________________
2. **Start:** signed-out disposable test profile, not your normal account.
   **Action:** open Sign In, choose Create Account, return to Sign In. For an unknown
   test endpoint inspect the first-use trust prompt; cancel unless its pin is verified.
   **Expected:** navigation works without identity replacement; no automatic trust.
   Never replace the real server pin merely to test mismatch.
   - [ ] PASS  - [ ] FAIL — Notes: ____________________

## B. Presence and ordinary messaging

3. **Start:** A and C Online, ordinary verified contact conversation selected.
   **Action:** exchange numbered B1/B2 messages; background and reopen Motorola;
   send B3; leave all clients idle after ACK.
   **Expected:** each message once, real Delivered ACK, correct presence; history
   opens without a live peer route. Idle peer count may reach zero without logout.
   - [ ] PASS  - [ ] FAIL — Notes: ____________________

## C. Mailbox / offline delivery

4. **Start:** temporary encrypted server storage enabled on both existing profiles.
   **Action:** disconnect recipient network; send C1 and wait for Stored. Exit sender
   completely, restore recipient network, then reopen sender only after receipt.
   **Expected:** recipient receives once without sender; signed ACK clears mailbox;
   sender later reconciles Stored to Delivered, same logical message ID.
   - [ ] PASS  - [ ] FAIL — Notes: ____________________
5. **Start:** disposable recipient test with storage explicitly OFF.
   **Action:** take recipient offline and send C2; restore network.
   **Expected:** no unauthorized server storage; Queued while unavailable, eventual
   ordinary delivery once a route exists. Restore your preferred setting afterward.
   - [ ] PASS  - [ ] FAIL — Notes: ____________________

## D. Android FCM/background delivery

6. **Start:** Motorola remembered sign-in and notifications authorized, normal battery
   policy, working foreground messaging; do not force-stop Android.
   **Action:** press Home; send D1. Lock screen and send D2/D3/D4, one at a time.
   Include one Stored message whose Windows sender is then fully exited.
   **Expected:** opaque FCM wakes bounded work; real fetch/decrypt/history/ACK precede
   notification; delivery without opening/unlocking phone, ordered and duplicate-free.
   Record latency and thermal/job stop reason if delayed; do not bypass power policy.
   - [ ] PASS  - [ ] FAIL — Notes/latencies: ____________________

## E. Voice / AEC

7. **Start:** existing Windows voice-capable peers, headphones for initial check.
   **Action:** start accepted 1:1 voice; confirm both directions and saved NS/AGC/input/
   output settings. End/restart call after changing one preset. Separately compare
   AEC ON/OFF using a controlled low-volume speaker setup if safe.
   **Expected:** existing framing/lifecycle unchanged; presets apply next generation,
   diagnostics reflect effective AEC/render reference; no squeal or new dropout.
   This is Windows AEC testing, not a claim of Android voice support.
   - [ ] PASS  - [ ] FAIL — Notes: ____________________

## F. Windows live file transfer

8. **Start:** A and C verified direct conversation; disposable small binary,
   `résumé-测试.bin`, and two files with the same basename in different folders.
   **Action:** click composer paperclip, then Send file to selected conversation…;
   choose each file. Recipient opens paperclip/File transfers, clicks Accept… and
   selects a test destination folder. Also send a larger file below 256 MiB.
   **Expected:** visible bounded progress; Complete — SHA-256 verified only after
   full verification; exact bytes (compare Get-FileHash), unique ID-prefixed names,
   no overwrite. Drag/drop is not required or claimed.
   - [ ] PASS  - [ ] FAIL — Notes: ____________________

## G. File-transfer reconnect/process restart

9. **Start:** accepted sufficiently large transfer in progress, original source retained.
   **Action:** briefly disconnect receiver network; reconnect. Repeat with normal
   sender exit/relaunch, then receiver exit/relaunch using the same profiles. Open
   File transfers after each restart. Cancel a separate disposable transfer.
   **Expected:** verified durable prefix resumes, no false completion or duplicate
   final file; Cancel removes only its owned partial. No profile/key replacement.
   - [ ] PASS  - [ ] FAIL — Notes: ____________________

## H. Archive enrollment/revocation

10. **Start:** A/B same-account legitimate devices Online, no archive consent yet.
    **Action:** Windows Settings → Privacy & Security → Archive devices; Android
    Settings → Archive devices. Refresh archive devices on both and compare Peer IDs.
    Explicitly Trust archive authorizer A on both devices (confirm Peer ID). On A,
    select A, enable May serve historical messages and Authorize history access;
    select B, enable May receive historical messages and Authorize history access.
    Refresh both until these signed grants are visible.
    **Expected:** no access from login alone; exact intended capabilities/authorizer,
    no friendship/DM permission manufactured between same-account archive devices.
    - [ ] PASS  - [ ] FAIL — Notes: ____________________
11. **Start:** B actively synchronizing from A with several pages/files remaining.
    **Action:** on authorizer A select B → Revoke access → Confirm; refresh B.
    **Expected:** future archive bytes stop; existing committed legitimate history
    stays. Re-enrollment requires explicit new consent; stale grant cannot resume.
    - [ ] PASS  - [ ] FAIL — Notes: ____________________

## I. Text history sync

12. **Start:** restored explicit A-serve/B-read authorization; A has numbered older
    messages with C missing on B; both know authenticated ownership for those peers.
    **Action:** B selects A → Synchronize from selected archive (Windows), or
    Synchronize from this archive under A's device card (Android). Observe progress;
    open the corresponding conversation after completion.
    **Expected:** bounded text pages; original IDs/order/sender attribution retained;
    archive-attested/legacy provenance is not falsely upgraded to original signature.
    No send outbox entry, new message ACK or fabricated Delivered state from import.
    - [ ] PASS  - [ ] FAIL — Notes/counts: ____________________

## J. History restart/resume/dedupe

13. **Start:** A has enough missing history for several pages; B sync running.
    **Action:** Pause synchronization, close B normally, reopen the same profile and
    resume from A. Repeat synchronization after completion. During a further scan
    deliver a fresh ordinary message from C to B.
    **Expected:** durable cursor/pause survives; resumed/replayed/live data produces
    one logical message each, no skips or timestamp-based cursor errors. Completion
    does not continuously rescan; a new scan is explicit.
    - [ ] PASS  - [ ] FAIL — Notes: ____________________

## K. History attachments/media

14. **Start:** A possesses completed F test files and catalog, Windows reader B has
    receive authorization and completed text/catalog sync from A.
    **Action:** B Settings → Privacy & Security → Historical attachments → Refresh
    attachments; use Next attachment page if needed. Keep A selected as archive
    source; select Unicode/small/large file → Download selected attachment… → choose
    test folder. Inspect File transfers; compare final SHA-256 with A.
    **Expected:** metadata alone downloads no bytes; explicit request uses existing
    TransferManager, full verified file, safe unique filename, no overwritten files.
    Select same logical file again: no second transfer for an already possessed file.
    Different logical files with identical content are not promised global dedupe.
    - [ ] PASS  - [ ] FAIL — Notes/hashes: ____________________
15. **Start:** disposable archive file downloading, source stays unchanged.
    **Action:** restart sender and receiver separately, then resume; cancel a second
    request. For a third disposable source move/remove it only after catalog sync,
    then request it; restore the test source afterward.
    **Expected:** bounded durable restart/resume; partial never reported complete;
    missing source reports failure without repeated request spam. Cancel clears the
    failed request before explicit retry. Revoked old-grant partials do not silently
    rebind to a new grant. Hash mismatch/error cases also have automated coverage.
    - [ ] PASS  - [ ] FAIL — Notes: ____________________
16. **Start:** Motorola reader has completed sync.
    **Action:** inspect attachment-record count and text history after cold launch.
    **Expected:** catalog persists, files not falsely described as downloaded.
    Actual Android download/view UI is deferred: do not mark Android file media
    acceptance PASS from the count alone. Run built provider instrumentation later
    only with test-namespaced aliases/storage, preserving the normal identity.
    - [ ] PASS  - [ ] FAIL — Notes/deferred: ____________________

## L. Settings/default persistence

17. **Start:** disposable/new profile with no saved feature choices; explicit archive
    authorization still required independently.
    **Action:** inspect mailbox and Windows AEC defaults; after archive Refresh inspect
    History synchronization. Set each OFF, restart, verify OFF; then restore intended
    settings. With History OFF inspect previously imported messages and grants.
    **Expected:** missing safe/useful defaults ON; explicit OFF survives; no automatic
    archive grant, no deletion of history/grants/cursors, no bytes while History OFF.
    - [ ] PASS  - [ ] FAIL — Notes: ____________________

## M. Final restart/regression check

18. **Start:** all intended settings restored; no active test transfers/calls.
    **Action:** close/reopen clients, verify identity/pin/history, send one bidirectional
    message, repeat sync, and idle. On existing supported Windows setups briefly
    exercise accepted group text, group voice and group screen share; stop cleanly.
    **Expected:** no identity changes, duplicate history, stuck workers, eager permanent
    archive routes or ordinary messaging regressions. Record participant count and
    route from diagnostics; do not infer 3+ users/multi-viewer/TURN from two-user tests.
    - [ ] PASS  - [ ] FAIL — Notes: ____________________

Stop and report failed item, client versions, safe operation/stage/result diagnostics
and timing. Do not weaken trust, erase profiles or redeploy the server to hide failure.
