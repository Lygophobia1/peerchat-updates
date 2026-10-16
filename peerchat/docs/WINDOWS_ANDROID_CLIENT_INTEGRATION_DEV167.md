# Windows and Android client integration dev167

Dev.167 is a client integration candidate for chronological conversations, bounded Android background History Sync, negotiated transfer performance and accessible contact/invitation management. Physical Motorola acceptance is pending. No phone installation, live Pi deployment, account reset or historical identity migration is included.

## Repository and checkpoints

Existing branch: `codex/native-unicode-emoji`. Worktree: `C:/Users/Livel/.codex/worktrees/native-unicode-emoji/Create 2D Game/peerchat`. Baseline `b3c32ab` retains the dev.165 messaging and dev.166 independent-device delivery implementations. Production candidate source: `7f8456d`.

- `66cb35d`: chronological native history, protected in-memory ordering keys and Windows/Android presentation.
- `93f84d2`: chronological older-page anchors and a 128-conversation cache limit matching Android's bounded contact publication, with the same 32768-key total budget.
- `52dee39`, `4c434a5`: bounded background archive leases and fail-closed terminal-state scheduling.
- `738e077`: Android contact and transfer-settings screens.
- `b0fb3de`: corrected synthetic UI fixture timestamps without weakening visibility assertions.
- `12d35f4`: negotiated portable transfer pipeline, protected tuning and upload/download limits.
- `7f8456d`: shared presenter/backend integration, scoped search and safe eligibility-stage diagnostics; version 4.0.2-dev.167.

## Chronological ordering

The native DM page used arrival rowids while several projections used logical clocks or assumed presenter order. Late mailbox/archive records could therefore alter the visible page or appear above newer text and pictures. Display now uses the original authenticated timestamp, then sender device and message ID. Stored timestamps, logical clocks and IDs are not rewritten. Database sequence values remain opaque page anchors, not timestamp substitutes. Archive traversal and live ACK cursors remain separate.

Protected ordering keys are derived into a bounded process-local cache; no decrypted timestamps or content are added to persistent indexes. Warm reads decrypt selected payloads. Cold reads, eviction and histories over 32768 keys require a protected scan; this can cost more CPU and latency than arrival-only paging. The 128-conversation bound avoids repeatedly evicting small histories when Android publishes all its bounded contacts. Incorrect sender clocks remain incorrect wall times: chronology cannot infer the intended send time.

Windows collections, device-history merging, previews and older anchors, plus Android snapshots, group projections, previews, older anchors and text/file timeline use the same tie-breakers. Existing legacy signed-ledger group pagination remains its legacy logical-token path; authenticated encrypted group history uses the new native chronological paging. No signed group ledger is rewritten.

Validation: new native chronology tests cover late/equal-time records, sender ties, cross-device anchors, external inserts, live/history duplicates, protected reopen, erasure, cache hits/eviction and protected tuning CAS. Existing SQL, protected-record and archive-history tests passed in the focused ordering checkpoint. Windows model tests and final Android tests cover rendered ordering and newest-message/composer visibility. See `MESSAGE_CHRONOLOGY.md` for cache and cursor semantics.

## Background History Sync

Dev.166's finite background lease admitted mailbox work only and returned before archive progress, event handling and relay startup. The wake job could finish when mailbox work drained even though an authorized archive scan remained pending.

The same runtime and existing 30-second JobScheduler job now lease archive capability separately from ordinary friend/group live-route capability. They refresh existing authenticated authorization, resume the existing native engine and retain bounded pending retries. Mailbox completion cannot mask pending archive work. Native terminal failures, revoked authorization, pause and disabled policy do not create futile retry loops. Stale completion is displayed as Waiting while the prior completed result remains separate.

This enables resumable work during execution windows, not uninterrupted background networking. Remembered authentication, an available authorized source and Android network/Doze/quota/vendor policy still control progress. Extra windows consume bounded CPU, network and metadata bandwidth. No permanent foreground service, independent sync engine or unrestricted wake lock was added. Source/relay unavailability stays pending rather than becoming false completion. The supplied dev.166 relay failure cannot be attributed to a specific physical cause from host evidence alone.

Focused background tests passed 28 cases, with 3 final terminal-state scheduling checks; the complete final Android suite includes those checks. Details and OS restrictions: `ANDROID_BACKGROUND_HISTORY_INTEGRATION.md`.

## Transfer capability and settings

Android used the shared TransferManager, but its portable endpoint omitted the existing pipeline capability, the adapter did not bind the current winner's negotiated capability to TransferManager, and the portable send guard rejected a full 64 KiB payload plus metadata. Its protected journal also inherited no-op tuning methods. These omissions prevented optimized framing and persistent customization.

The endpoint now advertises existing v2 support only for ordinary authenticated file routes; the adapter binds support to the actual winning transport before receiving and ticking transfers. File packet bounds use the established 70000-byte protocol bound; TLS/relay carriage remains fragmented at its existing 64 KiB bound. Archives/group-control lanes do not acquire pipeline capability. Old peers still use 32 KiB × 1 framing.

Missing profile defaults now request 64 KiB × 16. Windows upload/download defaults are unlimited. Android defaults are 10 Mbps upload and download, explicitly permitted by this milestone; no evidence supports forcing 5 Mbps. Both platforms allow separate user caps, including unlimited. Android offers Unlimited/10/5/Custom presets with exact decimal Mbps-to-bits conversion. Existing protected user settings and per-transfer overrides remain authoritative. Historical diagnostics and retained transfers may still show their original v1/32 KiB settings; verify negotiation on a new transfer.

Upload caps retain existing aggregate token accounting. A download cap paces existing durable cumulative Resume ACK credit without sleeping the runtime, changing ciphertext, or keeping another payload queue. Up to the negotiated window can arrive before credit pacing takes effect, so capped downloads can be bursty. Retransmissions do not count again. Very low custom caps can encounter normal inactivity/retention limits. Unlimited does not bypass congestion control, memory bounds, receiver backpressure or Windows media priority (active media still reduces file scheduling credit).

Android tuning uses a new device/profile-protected supplemental record with compare-and-swap; Windows retains its existing upload tuning format and adds an isolated protected download-limit sidecar. No journal or message format is rewritten. Corrupt optional tuning pauses file payload but now cannot abort reopening authoritative journal records or ordinary messaging endpoints. It is not silently deleted or reset.

Expected benefit is larger authenticated in-flight credit and durable settings, not a measured Motorola throughput claim. Memory remains bounded by the existing maximum 16 outstanding chunks, four active transfers and transport output limits. More in-flight data and faster payload processing can increase battery/thermal/network use; the Android cap and explicit user settings manage that tradeoff.

Focused tests passed all 32/64 KiB × 1/4/8/16 cases, delayed/duplicate/stale ACKs, old-peer fallback and no-capability rejection. Receiver-cap tests validate modeled credit/time ceilings, duplicate safety, verified completion and unlimited switching. Protected profile tests cover first-run defaults, custom preservation, separate limits, reopen, unchanged-write suppression and corrupted supplemental settings with existing journal records. Real Schannel portable-endpoint tests passed bidirectional negotiated 64 KiB × 16 picture/file transfers through the production manager. These host fixtures do not measure phone storage, battery, Wi-Fi/cellular throughput or Pi network capacity.

## Contacts and invitations

Android lacked a Chats-level entry point and exposed only exact-name lookup. The current server already supports authenticated, public-directory-only, bounded prefix search and friend cancellation. No server extension or deployment is needed.

A circular Chats + opens Find People, incoming requests, outgoing requests, group invitations and authorized contacts. Search is debounced 350 ms, bounded to 15 results, cancellation/context guarded and case behavior follows the existing server. Lookup uses the same authenticated account with a short-lived request outside the messaging owner lock and persistent RPC channel. This adds a TLS lookup cost but prevents slow typing queries from blocking message processing. Results cannot automatically trust devices; existing native fingerprint checks remain authoritative.

Relationship state prevents duplicate request submission; incoming approval/decline and outgoing cancellation use established operations. Invitation acceptance uses existing server/native membership and epoch-key operations. A group opens only when native membership/key readiness permits. Pending invitation APIs expose a group ID and inviter, not an authenticated private group name, so the UI displays the ID rather than inventing a name. Offline writes are disabled; reconnection/refresh restores authoritative pending lists. No history-access grant or group key is created by a UI action.

Focused contact/presenter/settings UI tests passed 32 cases; all 10 UI-host cases passed after fixing a fixture that assigned all 300 synthetic messages timestamp zero. The original newest-message and composer visibility assertions remain intact.

## Eligibility diagnostics and compatibility

The previous generic IllegalStateException did not distinguish transport/RPC failure, a non-ready snapshot response, scope/format rejection or native verified-eligibility rejection. The collector now reports these bounded stages and fixed safe reason categories; no credentials, server payload or plaintext are logged. A later successful snapshot replaces the stale failure diagnostic. No authentication, trust, revocation or snapshot freshness check was weakened.

The earlier physical anomaly's exact cause remains unproven without fresh stage diagnostics. Successful primary delivery still does not prove every eligible device received a copy. Dev.166's canonical-only unresolved fallback when no fresh verified snapshot exists at queue commit remains unchanged; later snapshots do not silently expand that message's frozen target set. Sent-message mirroring and broader fanout policy changes are outside this milestone.

Shared modifications: `storage.cpp/.hpp`, `transfer_manager.hpp`, `portable/reliable_endpoint.hpp`, `portable/peerchat_core.cpp`, `portable/transfer_storage.hpp`, `node.cpp`, `windows_transfer_io.hpp`, `windows_group_attachment_transfer.hpp`; Windows `App.cs`, `ChatViewModel.cs`, `TransferSettings.cs`; Android application/platform/presenter/UI components. Existing `TransferBridge.request` gains local operations 6/7 for tuning; no new encrypted message or server protocol is introduced. Internal C++ interfaces require rebuilding matching clients; do not mix Core DLLs between packages. Accepted dev.165/166 artifacts remain retained.

## Final validation and physical acceptance

Final Android ARM64 assemble, all 250 unit/UI-host tests and lint passed. Final complete Windows build, native dependency loading and package source/signature/version/ZIP byte audits passed. Native chronology, the actual final WPF chronology assembly, portable endpoint, transfer integrity/restart, pipeline, rate budget and protected preferences passed. JNI host tests passed repeated protected-profile login/resume and pinned-TLS messaging, six bidirectional deliveries, two offline queued IDs, supersession rejection and restart/deduplication. The authenticated two-recipient snapshot/mailbox fixture passed independent receipts across restart, and archive-first mailbox recovery passed with no history-generated ACK. Group History Sync passed 74 nonempty authenticated imports, paging, replay, revocation and protected reopen. Emoji and clipboard regressions passed.

The final media fixture passed a verified 10 MiB transfer across actual screen-stop, relay re-pairing and confirmed-prefix recovery. Group screen tests passed two real RTP/MFT receivers, removal isolation and TURN-only authorization guards. Group attachments passed both recipients restarting before key ACK and during download/finalization/receipt, duplicate replies, verified open/export, missing old key, epoch change and real TLS/server restart, using the unchanged dev.166 candidate server in an isolated fixture. Evidence: `F:/PeerChat-isolated-cache-tests/application-78c4a6bf1a2b43d1b6d140f150c9b946`. No live cache was accessed. Logs are retained under `build/dev167-*`.

### Controlled host transfer results

The final package's two native cores ran inside one managed harness with real TLS, protected storage, TransferManager and durable ACKs. Two blocking forwarding workers carried opaque TLS bytes in memory. There were no sockets, Pi or Android endpoint. Each single-run sample transferred 10485760 bytes; elapsed time starts after acceptance and ends when both sides report verified completion, excluding route setup. Concurrent integration work and storage scheduling introduce variation; these are not physical network or Motorola measurements.

| Requested settings | Accepted to complete | Confirmed payload rate | Process CPU | Peak working set |
| --- | ---: | ---: | ---: | ---: |
| 32 KiB × 1 | 2539.7 ms | 33.03 Mbps | 1687.5 ms | 69623808 bytes |
| 64 KiB × 8 | 849.0 ms | 98.81 Mbps | 1078.1 ms | 69144576 bytes |
| 64 KiB × 16 | 1341.8 ms | 62.52 Mbps | 1343.8 ms | 70197248 bytes |

The preferred 16-chunk configuration completed with integrity and improved this sample over the conservative baseline, but was not the fastest host sample. A larger allowed window is not proof of optimal throughput. No Android cap was lowered to 5 Mbps on this evidence. Full seven-configuration results and stage/ACK counters: `build/dev167-pipeline-final.log`.

### Packages

- Complete Windows ZIP: `build/development/4.0.2-dev.167/PeerChat-Windows-4.0.2-dev.167.zip`, 12333444 bytes, SHA-256 `24b3a3e3497bbfa983ad9fcc5ad0d08645fad47b7a68fd08e2f3f3388a2a05eb`.
- Android ARM64 debug APK: `build/development/4.0.2-dev.167/Android/PeerChat-Android-4.0.2-dev.167-debug.apk`, 37451059 bytes, SHA-256 `afc1647bb68bff515d0cd1e3002e12faec68a4c0510a8b6aea9c901d8b9bf155`.
- Production source checkpoint: `7f8456d95aad7c3fe61fdd1c3efcd72f6e2accd5`. APK signing certificate SHA-256 remains `db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`; application ID/signing identity are unchanged.
- Audit: `build/development/4.0.2-dev.167/messaging-repair-package-audit.json`. Earlier accepted packages remain untouched; preliminary dev.167 outputs are preserved in `build/dev167-preliminary-preserved`.

The handoff checkpoint following the production source contains only this report and package-audit support. Compiler intermediates are preserved under `build/msvc-intermediates-dev167-preserved`; ignored packages, fixtures and evidence remain available. Working-tree cleanliness is verified after committing the handoff. No merge, push, live deployment or phone installation occurred.

Install manually with `adb install -r` using the APK below; never uninstall or clear data. Unzip and run the complete Windows package, not the Client-only folder. Physical checklist:

1. Check late/live/mailbox/History Sync message order, equal-time stability, pictures between replies, previews and Older paging after restart.
2. With existing grants and source online, background/lock during History Sync; reopen and verify committed counts, no duplicates and accurate pending state. Repeat with source temporarily unavailable and Wi-Fi/cellular change where available.
3. Send new representative files both directions; verify v2, 65536-byte chunks and window 16, confirmed Mbps, completed-file access, restart/resume and concurrent messaging. Test Unlimited, 10 Mbps, 5 Mbps and custom settings across restart; existing custom preferences must remain unchanged.
4. Confirm locked-screen message notifications, both-device/offline delivery, exact ACK behavior and overlapping History Sync. Do not infer all-device receipt from primary delivery alone.
5. Use Chats + for partial search, add/accept/decline/cancel, reconnect/restart pending lists, group invitation acceptance/decline and key-pending guidance. Compare any unexpected fingerprint through the existing verification UI.
6. Smoke-test accepted group attachments, clipboard screenshots, emoji/favorites, Windows voice/screen sharing while transferring, and automatic verified saving.

No physical background, throughput, battery or full integration acceptance is claimed until the user reports those results. No Android Voice/video implementation was started. No live Pi access, change or deployment was performed in this milestone.
