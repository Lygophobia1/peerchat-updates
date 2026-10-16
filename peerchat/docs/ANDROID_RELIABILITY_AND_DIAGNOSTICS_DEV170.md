# Android reliability and diagnostic export dev170

Dev169 is not physically accepted. This checkpoint prioritizes complete local diagnostic export, the production DM transfer dispatch failure, bounded connection observability and Android conversation auto-scroll. No Pi deployment, phone installation, profile reset, identity change or unrelated milestone is included.

## Diagnostic export

Android now offers Export Diagnostics through the system document picker and Share Diagnostics through a read-only FileProvider URI. The report is a frozen copy of the existing immutable safe application projection. Formatting and file writing run off the UI thread, use 8 KiB buffered UTF-8 writes, and never put the report into a clipboard or Binder Intent. Short Copy remains available. The screen preview is limited independently from export. The source budget is 32 Mi characters; oversized reports fail visibly rather than being silently truncated. Shared cache reports are confined to the diagnostics provider directory, with at most three generated files after a share and cleanup on subsequent sharing.

Windows had a confirmed 65,536-character truncation in both the full formatter and privacy filter, plus per-section capture clipping. Complete file export now bypasses those clipboard limits, captures all available current sections and bounded retained failures independently of the selected tab, and writes a frozen snapshot through the normal Save File dialog. A temporary file is committed only after formatting and writing succeed. The writer rejects source lines over 1 MiB and reports over 32 Mi characters explicitly. Current loaded message/transfer correlation is metadata only; it does not scan or export message history content.

Both exports redact credentials, tokens, private key material, plaintext fields, sensitive paths and filenames. Public message, transfer and device identifiers remain available. Android canonicalizes only the exact native `delivery_targets message=<32 hex ID>` prefix to `message_id` before generic plaintext redaction; arbitrary `message=plaintext` remains redacted. Reports are never uploaded automatically or sent through PeerChat transfers.

Tests preserve the exact end of a multi-megabyte Unicode Android report and a 3,309,377-byte Windows report, including correlation fields after the previous clipboard limit. They cover frozen snapshots, privacy filtering, save cancellation and write failures. The actual Android FileProvider instrumentation test compiles but awaits device execution: the isolated software emulator did not boot within its five-minute bound, and was stopped without touching phones or existing AVD data.

## Connection and transfer findings

The ordinary friend-file callback in `portable/peerchat_core.cpp` used `archive::transferPacketId` whenever the archive-transfer owner existed. That parser intentionally accepts only version 1 and small archive chunks. Consequently, ordinary negotiated version-2 control/payload records could fail before reaching TransferManager, at the generic `transfer_receive` stage. The earlier endpoint interoperability fixture bypassed this production callback and therefore did not cover this condition.

The correction uses the existing ordinary v1/v2 `transfer::packetId` at that friend-file boundary while retaining the archive-transfer ID exclusion. The archive parser, admission and storage remain strict and separate. TransferManager, wire formats, chunk/window choices, caps, encrypted records, durable ACKs and protected journals are unchanged.

The pre-fix full-core JNI test reproduced the exact reported signature: ArchiveBridge initialized, actual cached-file offer and accept through TransferBridge, negotiated 64 KiB/window16, followed by 109 encrypted input bytes rejected at `transfer_receive`, zero payload progress and route closure. Evidence: `build/tests/native-transfer-before-fix-runtime.log`. This is an implementation-level causal reproduction, not a claim that the Motorola's POSIX filesystem or physical Wi-Fi was executed by the Windows host fixture.

The new full-core restart test also found a native owner reference cycle: `Instance` owned `archiveGroups`, whose group-context callback captured a strong shared pointer back to that same `Instance`. Removing the profile from the registry therefore did not destroy it after History Sync initialization, even with native routes explicitly closed. The protected receiver partial existed, but exclusive file handles remained held and reopen failed with `Restart validation failed: source/partial missing or changed`. The lifetime correction uses a weak reference, obtains a temporary owner only while reading its existing verified group context, and returns unavailable when that owner no longer exists. It does not alter group membership, historical-access checks, keys, grants or record formats. The same full-core test now passes resource-release assertions and resumes the protected partial to verified completion. This correction is checkpointed separately from the ordinary-file parser correction.

Bounded local observations now retain 64 network operation events with UTC timestamps, elapsed durations, last success/failure and fixed failure categories. Sixteen completed relay attempt snapshots and up to 32 active attempts include stage durations, public peer/attempt context, last completed stage and cleanup state. An authenticated-peer stage is recorded only from the existing native ready event, not merely relay pairing. No event body, token, password or exception message is retained. Mailbox and event retry deadlines remain visible. The existing 64 transfer rows and 2,048-character row bounds remain, but the unrelated 32 KiB total cut is removed; any per-row/row-count clipping is explicit. This bounds the transfer report to about 132 KiB without dropping its last ordinary rows. Transfer identity contexts come from existing typed snapshots and omit filenames and paths.

The physical dev168 observations included 19,561 ms owner wait, 14,001 ms tick maximum and 8,519 ms projection maximum. These are not improved physical measurements. A proposed scheduling rewrite was rejected by safety review and remains unapplied; there is no duplicate relay servicing or new off-lock native owner. Existing bounded RPC work remains serialized. The dispatch fix prevents a demonstrated file-control rejection from terminating an otherwise valid peer route; its physical effect and remaining independent latency require the next test.

## Android auto scroll

The previous effect recomputed near-bottom status after timeline growth and had no unconditional initial scroll. New arrivals could therefore make a previously following viewport look like older reading, and a long conversation could open above its newest messages.

The updated view tracks the user's anchored row separately from timeline size. Opening a conversation and explicitly sending or choosing Jump to latest moves immediately to the newest row. Incoming changes and viewport shrink follow only while already following. Reading older messages preserves the stable-key anchor, including prepended history/pagination; a Jump to latest control remains available. Progress updates do not trigger unnecessary animations. Four focused Compose tests pass, including viewport shrink and prepended history. Actual keyboard/phone behavior remains a physical acceptance item.

## Compatibility and costs

Messages, attachments, profile identity, grants, keys, encryption and server protocols are unchanged. Diagnostic histories are bounded in memory and have no periodic database writes or upload traffic. Export adds disk I/O only on explicit save/share, with a bounded private cache. Formatting a very large report consumes CPU and storage proportional to its size; it does not hold the messaging owner gate while writing. Android public transfer-report retention can use approximately 200 KiB more UTF-16 string memory than the previous 32 KiB-character cut. No physical battery or connection-latency improvement is claimed. No Android Voice, transfer-speed tuning or Pi group-cache rerouting is included.

## Validation and packages

Focused export, privacy, typed correlation, connection-stage and scrolling tests pass. Full Android host regression passes 284 tests with zero failures/errors; lint has zero errors and 25 warnings. Physical-device instrumentation compiles. Evidence: `build/dev170-android-regressions.log`.

The final full-core JNI fixture passes a 16 KiB file, 2 MiB files in both directions, exact completed bytes/integrity, five authenticated rejection cases, a 1 MiB interrupted protected-journal restart/resume, released old profile facades, and eight messaging/Hello/trust/reopen cycles including lost-ACK restart and deduplication. Rejections cover an archive transfer ID on the ordinary v2 lane, unsupported version, zero negotiation, malformed private ID, and a valid control followed by a malformed record in the same input without stale-ID attribution. The native two-byte status and bounded diagnostic contracts remain intact. Evidence: `build/tests/native-transfer-lifecycle-after.log`, fixture `build/tests/reliable-profile-jni-0c3c4e4555254eb8b61c8d06ee1e769c`. Repeatable command: `tests/test-reliable-profile-jni.ps1 -TransferFiles`. Production core SHA-256: `c9fce7cd1b5000ad6181a7de0b5c43235b7fb91b9afde6f8bcea43d566f77e06`.

Native test development failures are preserved rather than erased: initial host file-handle/test-helper setup errors, the exact pre-fix 109-byte failure, the pre-fix owner-release assertion and restore validation timeout. The final fixture passes after correcting the two production defects and the isolated host adapters. No host fixture is described as proof of Motorola POSIX storage, Keystore performance or physical network throughput.

Windows focused tests pass complete 3.31 MB UTF-8 export, privacy, canonical delivery correlation, cancellation, atomic write failure, frozen UI snapshot and selected/unselected conversation metadata. The production managed desktop and mailbox reliability regression compiled and passed. Windows all-component build and native dependency audits pass: `build/dev170-windows-build.log`.

## Checkpoints and release files

Existing worktree: `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`, branch `codex/native-unicode-emoji`. Starting checkpoint: `d2af84e`. Relevant commits: `05ed788` Windows complete export, `63928cd` Android save/share, `3bd9694` and `95ce03d` correlation privacy tests/correction, `7bab13f` auto-scroll, `235faa1` bounded stage projections, `0265648` ordinary v2 dispatch and `9153c0d` native owner lifecycle plus production JNI regression. Package source: `9153c0df3ba5fa9146177131a6ae124f4f8cb536`. Later report-only changes do not alter the production package source.

Final Android ARM64 build passes: `build/dev170-android-release-build.log`. Final source/version/signature/dependency/ZIP audit passes: `build/dev170-package-final-audit.log`. APK certificate SHA-256 remains `db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`.

Relative to the repository root:

- Windows: `build/development/4.0.2-dev.170/PeerChat-Windows-4.0.2-dev.170.zip`, 12,339,909 bytes, SHA-256 `1fd0061fc78792183fdfc643ab2c4bdd6f492452b65852c7fe5cc81210c67055`.
- Android: `build/development/4.0.2-dev.170/Android/PeerChat-Android-4.0.2-dev.170-debug.apk`, 26,593,477 bytes, SHA-256 `083928839496ace6470a43cc890c9b7046e7ca32bc5b29260328094a177fd68d`.
- Machine-readable audit: `build/development/4.0.2-dev.170/messaging-repair-package-audit.json`.

The initial incremental APK contained about 11 MB of obsolete ZIP space. A fresh package reduced it from 37,528,886 to 26,593,477 bytes without changing any of its 232 uncompressed entries. Signature and package audits were repeated afterward. The first, not-handed-off audit outputs remain under `build/development/4.0.2-dev.170/pre-final-audit`; earlier accepted releases are untouched. Generated screen-media object files are preserved in this build's ScreenMedia directory. No software was installed or deployed.

## Manual acceptance

1. Replacement-install the versioned APK without clearing data; open the matching Windows build with the existing profile.
2. On Android, save Export Diagnostics with the system picker, then Share Diagnostics through another installed application. Verify the saved UTF-8 report contains its last section and is independent of PeerChat transfers. Check Windows Export Diagnostics likewise.
3. Send text and a small image/file in both directions, followed by a multi-megabyte file. Verify exact completion, filenames and integrity; retry/resume a retained transfer after restarting.
4. Check conversation opening, outgoing messages, arrivals while at bottom, deliberate older reading, Jump to latest, keyboard changes and loaded older history.
5. If a connection, mailbox, eligibility or History Sync failure remains, export full reports from both matching sessions. Assess History Sync by durable imports with the exact authorized source online, not relay pairing alone.

Stop for physical acceptance. Do not deploy the Pi or resume unrelated milestones.
