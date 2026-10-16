# Android local history and send responsiveness dev172

## Status and scope

Dev.172 is ready for manual physical testing. Windows and Android text messaging remain physically accepted at dev.171; physical acceptance of these Android UX changes is pending. Offline DM attachments remain a separate proposal requiring approval in `OFFLINE_DM_ATTACHMENT_CACHE_PROPOSAL.md`.

Worktree: `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`.
Branch: `codex/native-unicode-emoji`. Baseline: `73942a8`. Client implementation checkpoint: `b2cfd09`.

No live Pi inspection/change/deployment, phone access/installation, profile migration, native API change, encryption change, protocol change or database schema change occurred. Existing packages are preserved. The Windows package is a matching-version compatibility build; its application behavior is unchanged.

## Confirmed causes and repairs

### Local history

`MessagingBackend.restoreLocalLocked` called the full publisher before returning its protected local context. The publisher reread history for every contact on each projection. This did not require server authorization, but delayed the first list and repeatedly performed unrelated protected-history work. The presenter also loaded protected preferences before beginning local restoration.

Startup now publishes the protected DM conversation index immediately after verifying its local device binding. It does not install cached contacts as authenticated native friend/route authorization. Selecting a DM reads the existing native eight-message page outside the application's RPC gate. JNI still owns the native instance lifetime and mutex; an immutable account/profile/server context is checked again before returning the page. A stale read after reset/account change is rejected.

`LocalHistoryProjection` is a disposable display cache, not a second history store. It holds at most 128 eight-message pages, refreshes at most one account per publisher pass, invalidates on messages, receipts and history revisions, and has a ten-second fallback refresh for otherwise unobserved changes. The selected account is prioritized. Existing native paging, protected records, durable cursors and ACK ownership are unchanged.

Limitations: the first offline DM list requires an existing protected local index. Cold-start group discovery still requires the existing authenticated group projection; this milestone does not invent an offline group membership index or weaken group admission. A selected known DM can be read without authentication; this is not a claim that all services work offline.

### Navigation

Late startup/resume completion unconditionally selected Home even after the user opened a conversation. Startup completion now clears its status without replacing Conversation, Details or another user-selected page. Saved selection uses Android saved-state handling and an endpoint-pin/account owner key. It waits for the selected conversation to appear, including a delayed group projection, rather than consuming restoration too early. Account changes clear the selection. A genuinely absent conversation gets an explicit unavailable explanation; real authentication failures remain fail-closed.

### Outgoing display

The existing send command waited behind application work, durably queued through native storage, then synchronously published all contacts before returning. The UI therefore displayed nothing while this work ran.

`beginLocalSend` now publishes a UI-only Saving locally bubble before launching the suspend command. Only a successful existing native durable command binds the actual message ID and changes the bubble to Queued. Native history then replaces it by exact ID, never by text matching. A racing native snapshot cannot create a second outgoing bubble. A failed local save retains the draft and offers explicit review/retry; it does not fabricate a queue entry or ACK. A later projection/executor shutdown cannot convert an already successful durable command into a failed send.

Native send no longer waits for a full UI projection before returning. Network hints and projection run best-effort on the existing worker; the existing delivery owner remains authoritative. No retry engine or network worker was added. New typing survives an earlier send's completion. Account/generation invalidation releases busy presentation state while rejecting stale completion. Early draft edits are merged with protected preferences when loading finishes instead of being overwritten; local edit overrides are bounded and cleared on account changes.

## Timing boundaries and measured results

| Boundary | Before | After and evidence |
| --- | --- | --- |
| Physical phone first history | User reported 10–15 seconds | Pending phone retest; no synthetic result is presented as a phone measurement |
| Physical Send to visible bubble | User reported 5–10 seconds | Pending phone retest |
| Controlled local-list fixture | Authentication deliberately delayed 10 seconds | Local list available before advancing that delay; selected chat remains open when startup completes |
| Controlled Send fixture | Native durable return held behind an explicit deferred operation | Saving bubble exists synchronously before the coroutine starts, never Queued until release; exact returned ID replaces it |
| History projection work | All contacts' history per publication | One bounded account page at most per publication; no read for a fresh cached page |
| Network delivery latency | Existing accepted path | Not retuned or physically compared; no claimed network-latency improvement |

New process-local diagnostics distinguish SQLite database open, storage-adapter initialization, native profile binding, protected local index, native history page, application-gate wait, durable local commit, first local-list frame, first populated chat frame, first send-bubble frame and durable command return. Existing profile/authentication, contact, mailbox, eligibility and archive timing diagnostics remain separate. UI frame observations run after a Compose frame clock callback; they are not touchscreen-latency or network-ACK measurements. Empty histories have no populated-chat sample.

## Cost and security

The display cache is bounded to 1,024 message rows, with native message-size limits unchanged. Older UI pages retain their existing bounds. Provisional sends and frame observations are bounded to 16; local draft overrides to 128, with existing text-size limits. Cache misses and ten-second fallback refreshes add native reads only on normal publication; no new service, thread, high-frequency timer, database sampling, server traffic or battery wake owner was added. Protected preferences use existing storage. Live timing summaries are in memory and contain stages/counts/durations, not message text or keys.

The independent review identified draft loading, premature saved-selection consumption and stale busy-send cleanup defects during implementation. Each was corrected, with regression coverage. The read-only native page path was reviewed for lifetime/scope safety. Accepted Android native libraries are byte-identical to dev.171, verified from the signed APKs.

## Validation

- Android: 296 host tests, zero failures/errors; debug lint passes with 0 errors and 25 warnings; debug APK compiles. `build/dev172-android-final.log`.
- Focused tests cover bounded cache reads and invalidation, device-binding cache rejection, provisional/queued/failed states, duplicate presentation, snapshot races, delayed authentication/navigation, delayed group restoration, early preference loading, typing during send and account-change stale completion. `build/dev172-ui-final.log` and final unit-test XML.
- Production JNI/Profile host regression: eight protected profile reopen/message/ACK cycles; lost-ACK retry with stable ID and deduplication; two 2 MiB file directions; five authenticated rejection modes; journal restart/resume. `build/dev172-jni-regression.log`. This uses Windows host SQLite/DPAPI and Java TLS, not Motorola Keystore/POSIX/lifecycle acceptance.
- Authenticated isolated application/server fixture: real TLS snapshot to managed wire/native admission, two independently encrypted recipient receipts across restart; first ACK leaves the second pending; unpinned sibling rejected. `build/dev172-eligibility.log`.
- Archive/mailbox overlap: durable signed receipt only after actual authenticated mailbox receive; replay/restart dedupe; tampered/wrong-account records rejected; history alone creates no live ACK. `build/dev172-archive-mailbox.log`.
- Windows mailbox fairness/eligibility regression passes. `build/dev172-windows-mailbox.log`.
- Windows client/updater/screen-media build and restricted-search packaged native dependency checks pass. `build/dev172-windows-build.log`.
- Package audit verifies versions, unchanged Android signer and native payloads, Windows ZIP/runtime identity and exclusion of private profile/database/key files. `build/dev172-package-audit.log` and `build/development/4.0.2-dev.172/local-first-package-audit.json`.

## Packages

Windows ZIP: `build/development/4.0.2-dev.172/PeerChat-Windows-4.0.2-dev.172.zip`, 12,339,552 bytes, SHA256 `064c9aaabcf504523bb2ac11b5c5c1e800e33264084ad37e2c9c783139bdaa36`.

Android APK: `build/development/4.0.2-dev.172/Android/PeerChat-Android-4.0.2-dev.172-debug.apk`, 27,116,476 bytes, SHA256 `e9d3530286bdd218f015384dc978bec6411626e30fac9c978480e62f5d0ada53`.

Android signing certificate SHA256 remains `db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`. Use `adb install -r` manually; do not uninstall or clear storage. Open the extracted Windows executable using the existing profile.

## Physical acceptance checklist

1. Restart Android offline. Time the first local DM list and opening an existing chat. Scroll older history and check exact Unicode/attachments remain accessible through existing paths.
2. Open a chat while Android is connecting. Keep it open through authentication, refresh, background/resume and screen rotation; verify selection and scroll remain stable. Verify account switching never restores another account's chat or draft.
3. Tap Send online and offline. Confirm an immediate Saving locally bubble, then Queued only after local save, then real delivery states. Type a new draft during a pending save; it must remain. Do not repeatedly send the same test text to hide failures.
4. Reconnect/restart both clients and run Windows to Android and reverse text delivery. Check one logical bubble, stable history and independent receipts; test a representative file each direction without changing transfer settings.
5. Export diagnostics after startup and sending to compare local/storage/UI stages with authentication and actual delivery timing. Physical responsiveness and network latency remain pending until these observations are reported.

Stop here for physical acceptance and the separate offline DM cache design decision. No Android Voice, server implementation or unrelated roadmap work is authorized by this checkpoint.
