# Android dev168 physical followup

The supplied dev168 diagnostics identify a repeatable eligibility clock boundary and a separate native transfer-control rejection. Android remains authenticated, but presence does not establish delivery. No Pi or phone changes were made during this followup.

## Confirmed eligibility rejection

Native validation reports `snapshot_issued_in_future`: local UTC milliseconds `1792136141246`, snapshot issuance `1792136141656`, a difference of 410 ms. Validation took 157 microseconds. The prior minute-level clock comparison could not rule out this subsecond mismatch. Re-fetching newly issued snapshots can perpetually repeat the future-issuance rejection while the phone clock trails the server.

Checkpoint `bd6d2f6` changes only the Android response scheduling adapter. It keeps a correctly scoped authenticated response in bounded process-local memory when issuance is at most five seconds ahead, then submits the original unchanged bytes to native validation after local wall time reaches issuance. It never treats deferred bytes as authorization. The native account/device/generation, signature, binding, revocation, sequence and exact freshness checks remain unchanged. Expired evidence is discarded; account/generation reset and recipient removal discard deferred evidence. There is no sleep under the owner lock, server clock authority, added timestamp tolerance, persistence or protocol change. Larger clock mismatches continue through the existing native rejection path.

Focused tests pass for exact original bytes, no submission before issuance, native rejection after deferral, expiry, generation/reset isolation and larger mismatches. Existing wire-format tests also pass.

## Confirmed unnecessary event polling

The Android native event loop used `return@repeat` for an empty/closed queue. That skips one iteration rather than ending the drain, causing up to sixteen JNI exchanges per attached route even when no events exist. Each exchange also ticks native work. Checkpoint `4fdcf9a` stops the drain at the first empty/closed queue while retaining ordered processing and the sixteen-event maximum. Three focused tests cover empty/closed queues, ordered events and the busy-queue bound.

Physical timings show owner lock wait up to 19,561 ms, tick duration up to 14,001 ms, projection up to 8,519 ms and native relay service up to 2,009 ms. The empty-poll correction removes demonstrated redundant work; it does not prove the complete cause of those delays. Contacts, mailbox, eligibility, archive and projection costs remain separately visible.

## Transfer and history boundaries

Native exchange operation zero rejected input of 109 encrypted bytes at `transfer_receive`, taking four milliseconds. This is consistent with a small transfer-control record, not a large payload. Pending v2 sends show 64 KiB chunks, window sixteen, zero payload progress and three route changes. Exact control kind/predicate cannot be established from encrypted input length alone. Resume/reconnect checks remain fail-closed; no offset, identity, negotiation, chunk/window or speed limit is relaxed.

A real protected-journal/file-source/file-sink fixture subsequently reproduced an independent valid-flow defect: after restart, accepting an AwaitingAcceptance offer before the sender's fresh Offer arrives queued a default-v1 Resume response. The sender, already negotiating v2, rejected it at `transfer_control_version`. The pre-fix failure is retained in `build/tests/control-replay-bin/before-fix.log`. The minimal correction holds receiver replies while `offerSeen` is false; the existing authenticated fresh Offer establishes current version/negotiation and regenerates the proper response. Sender offset/high-water and version rejection predicates are unchanged. The physical 109-byte input cannot be identified as this same case; a v1 Resume would ordinarily have a different encrypted length.

Fixed-vocabulary receive diagnostics distinguish control identity/version, v2 capability, Resume direction/size/high-water/outstanding, and chunk/completion boundaries without exposing packet contents or filenames. Capacity and persistence exceptions retain their existing behavior.

History Sync still has no winning connection, no current imports, valid grants and no data rejection. A prior result remains visible separately from current activity. Eligibility failure is not evidence that archive pairing shares the same cause. The matching Windows session diagnostics are requested without requiring another reproduction, profile change or permission change.

The Windows transfer export captured at 07:48:46 UTC is not the matching failure: it describes a canceled 98,715,413-byte sender transfer from account 3/device `0cb421…c9ee4` to `35049f…8201b`, whereas the physical Android device is `61f70f…92b51b` and its new pending sends are small files. That export has zero payload/progress and no retained active route. Its recipient-account reference zero means the view has no current account association for that selected record; it does not establish cross-account routing or explain the Android rejection. No stored destination is changed based on this report.

## Validation and handoff state

Android host validation passed 267 tests, zero failures, with lint passing. Logs: `build/dev169-android-regressions.log` and `build/dev169-eligibility-final.log`. The final recipient-removal assertion also passed in the focused eight-test eligibility suite. The APK compiled successfully in `build/dev169-android-build.log`.

The real-journal control regression failed before the guard and passed afterward for both restored v2 and v1-to-v2 offers, small full-prefix resume before the sender receives its first ACK, duplicate controls, stale negotiation, completed-receiver/lost-completion-ACK recovery and invalid offset/direction/identity/capability rejection. All eight existing chunk/window pipeline combinations and durable restart/tamper/cancellation cases pass. Evidence: `build/tests/control-replay-bin/{before-fix,after-fix,pipeline,restart}.log`. The repeatable `tests/test-transfer-control-replay.ps1` run also passed; evidence is under `build/tests/transfer-control-regressions-d44eb014f03749289cb2ff7055e6bde9`.

Production Android Java TLS/JNI to Windows Schannel interoperation passed simultaneous 2 MiB plus 123 byte files both directions with unchanged negotiated 64 KiB/window16 settings and 10 Mbps fixture caps. This test is a local authenticated host path, not measured physical Wi-Fi throughput. Evidence: `build/tests/transfer-auth-interop-453f7de40d034d069e7a1865bbed5193`.

## Shared integration boundary

Android-only scheduling changes are in `EligibilitySnapshotExecution.kt`, `NativeEventDrain.kt` and `MessagingBackend.kt`, with focused tests. Shared changes are limited to `transfer_manager.hpp` (the restored-receiver reply guard and fixed rejection stages) and `portable/reliable_endpoint.hpp` (retaining the controlled rejection stage). There is no new exported native API, wire format, message/encryption format, storage schema or server requirement. Both client packages rebuild their native implementation against these headers. Existing Android and Windows peers remain compatible. No change is made to the Pi, transfer limits, history grants or device eligibility policy.

## Dev169 package handoff

Validated source checkpoint: `9e58fb7194d42f076dde611ab8d23c5af8b4a3fb` on the existing `codex/native-unicode-emoji` branch. Clock scheduling is checkpoint `bd6d2f6`, empty-event draining is `4fdcf9a`, recipient-removal coverage is `4c33885`, and transfer restart correction is `9e58fb7`. No merges, installations or deployments were performed. Existing accepted packages and user data are unchanged.

Windows build and native dependency audits pass (`build/dev169-windows-build.log`). Android ARM64 debug build passes; lint reports zero errors and 25 warnings. Version, unchanged APK signing identity, source checkpoint and ZIP byte audits pass (`build/dev169-package-audit.log`). Relative package paths:

- `build/development/4.0.2-dev.169/PeerChat-Windows-4.0.2-dev.169.zip`, 12,335,219 bytes, SHA-256 `8c7f85c6093ee53feb92a2a9c1097b538dbe1e4ba27735b0f056c3740132aec1`.
- `build/development/4.0.2-dev.169/Android/PeerChat-Android-4.0.2-dev.169-debug.apk`, 26,557,258 bytes, SHA-256 `f3a76cf1f09b023252606a070b54a1e1033cf1941214b2fb21afd2d84e35291a`.
- Machine-readable audit: `build/development/4.0.2-dev.169/messaging-repair-package-audit.json`.

For manual acceptance, replacement-install the APK without clearing data, and use the matching Windows client with its existing profile. Check native eligibility becomes cached after a short issuance wait; send text and a small image/file each direction; restart and accept a retained unfinished download; verify exact completion and no duplicate messages. Copy full diagnostics from both matching conversations if a failure remains. History Sync must be assessed separately with its exact authorized Windows source online and durable import counts, not merely a connected route.

Physical acceptance remains pending. The supplied Windows export is not the matching Android failure and that exact rejection remains unattributed. Stop for physical results and matching diagnostics; do not resume unrelated milestones.
