# Android voice completion backlog

## Current dev.181 checkpoint

The original investigation table below is retained as historical context. The
[dev.181 validation report](DEV181_VALIDATION.md) supersedes its implementation
statuses: genuine ARM64 receive integration, scoped foreground ownership,
explicit DM controls, group room/native integration and bounded diagnostics are
implemented and automatically tested. Physical acceptance remains pending.

Remaining gates and intentionally unimplemented features:

- P0: physical Motorola two-way DM and mixed Windows/Android group acceptance;
  ARM64 emulator translation failure is disclosed in the report, not hidden.
- P1: physical speaker/headset/Bluetooth, focus, lock/background, Wi-Fi/cellular,
  thermal/battery and sustained concurrent History Sync/transfer measurements.
- P1: automatic group route recovery after event-session loss (currently safe
  teardown and explicit rejoin), and cold-process incoming-call wake protocol.
- P2: actual active-speaker indication, per-source group diagnostics, full route
  picker and historical media-counter retention. Do not fabricate these fields.

Resume only after physical results or further authorization; no Pi deployment
or phone installation is part of this package handoff.

## Original audit state (superseded where noted above)

Authorized by the user's Android DM + group voice assignment. This is separate from the earlier networking audit's scope limits. No phone installation or Pi deployment is authorized for this milestone.

| Priority | Work | Status and acceptance gate |
| --- | --- | --- |
| P0 | Incoming-call consent during local recovery | Corrected at `30c7e25`; controller regression verifies no media before Answer, both identity orderings and seeded retry state. |
| P0 | Restore mute before media device startup | Corrected at `30c7e25`; pre-poll mute, recovery offer/answer and fail-closed preparation tests pass. Actual Android call validation remains required. |
| P0 | Genuine pinned ARM64 NetEq receive library | Blocked on supported Linux build environment; see dependency audit. Never enable capability based on mock tests. |
| P1 | Distinct call runtime ownership | Deferred until the backend exposes authoritative scoped call state and a real microphone service owns the lease. Current public snapshots lack authentication generation; do not infer lease authority from an active UI snapshot. |
| P1 | Android explicit Start/Answer, permission and ongoing notification | Not complete. Respect background microphone restrictions and scope notification actions to the intended call. |
| P1 | Genuine DM integration | Windows/Android and Android/Android, loss/recovery, revocation, permission denial and teardown; required before group application implementation. |
| P1 | Routing, focus, wired/Bluetooth and lock/background operation | Integrate existing AudioRecord/AudioTrack adapter; physical device evidence required for quality and power. |
| P1 | Android group application signaling | Reuse existing server room authorization and native topology after DM gate. Handle current roster/participation; bound supported group size. |
| P2 | Bounded voice diagnostics | Reuse real counters; no raw audio/keys or invented latency/speaking values. |
| P2 | Concurrent transfer/History Sync and call performance | Measure production media scheduling, jitter and CPU; do not infer phone performance from host fixtures. |

## Existing media backlog retained

Background fan-noise suppression, participant volume controls, active-video screen-share pacing and optional system-audio sharing remain separate future media improvements. This voice completion milestone does not reopen accepted Windows behavior or implement Android screen sharing/video.

## Validation recorded so far

- `test-android-voice-application.ps1`: red/green regression evidence in the architecture audit. Fake-media controller unit coverage only.
- `test-android-audio.ps1`: host PCM bounds/mixing/clipping/mute and production native JNI adapter with a fixture-supplied VM callback table passed; includes permission/unavailable failure, pre-muted start, stale generation, callback drain and concurrent diagnostics. No Java VM or physical AudioRecord/AudioTrack execution.
- `test-voice-topology.ps1`: relay policy, source/device/route generation fencing, direct SDP rejection, retire-one/preserve-other and 15 RTC lifetimes passed. No live TURN success claimed.
- `test-group-voice-session.ps1`: existing Windows receive adapter; one encode/fanout, framing, authorization, incremental membership, mute/deafen, retired callbacks and 30 lifetimes passed. Not Android multiparty acceptance.

Accepted dev.180 packages remain unchanged. No new voice test package has been declared ready.

Host evidence directories: `build/android-media/host-tests` (PCM/JNI fixture logs), `build/tests/voice-topology-6c5ca5474b704186b59bbb8e91b3cec2`, and `build/tests/group-voice-3b7fa884122b4ae18bb22a24fdb99586`. These last two runners report their assertions to the terminal and retain compiled fixtures; they are not physical-device traces.

## Resume checkpoint

Continue on `codex/native-unicode-emoji` in the same worktree. Preserve `be502d2` and the immutable dev.180 packages. Native safety fixes are checkpointed at `30c7e25`; the following documentation checkpoint records this audit. Linux setup approval or a suitable existing build host is required before dependency compilation can proceed. Do not enable Windows optional features, reboot, install on the phone or deploy voice changes to the Pi as part of merely resuming the audit. Once a supported Linux environment is available, follow the dependency acceptance gates and architecture sequence above. Recheck working-tree state before editing.
