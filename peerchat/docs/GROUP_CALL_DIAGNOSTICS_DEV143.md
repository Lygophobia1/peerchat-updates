# Group Voice & Screen Share Diagnostics — dev.143

Starting point: dev.142 / `181a02e`. This is a passive Windows diagnostics milestone, not a media optimization or protocol change. The existing GroupTelemetryProjection, group header, shared detachable window, graphs, pause, copy and Export tab are retained. Network is added to Group Calls. No Android or Raspberry Pi update/deployment is required. Native runtime DLLs are byte-identical to dev.142. Identities, grants, encrypted history, History Sync, transfer limits and media preferences are unchanged.

## Existing measurement coverage

| View | Actual existing source | Semantics and ownership |
|---|---|---|
| Overview | Authenticated DiscoveryClient GroupMediaState and existing verified ServerGroupRoster; cached group service JSON | Group/room IDs, epoch, roster/room revisions, local service state, and remote connected-route counts. Membership/viewer count is labeled authorization/participation, not transport connectivity. Voice and screen remain visible together. |
| Voice | pc_group_voice_snapshot, already published by GroupVoiceService | Opus route state and submitted/received payload Mbps; local capture/encoded/mixed 10ms blocks, playout busy/silence blocks, APM and AEC state, reference underruns and AEC delay estimate. These local processing counters are never assigned to a remote participant. AEC delay is not network latency. |
| Screen sharer | pc_group_screen_snapshot shared capture/encoder fields and each endpoint's existing ScreenShareMedia diagnostics | Shared captured/encoded FPS, dimensions and encoder queue drops. Each viewer's local sender endpoint exposes API-accepted access units and submitted-via-API FPS when available. API acceptance does not prove remote presentation. Shared capture/encoder processing costs and queue depth are not exported by the group bridge. |
| Screen viewer | Current source route's existing details, decodedFps/presentedFps and payload counters | Complete access-unit received FPS is distinct from RTP packets/sec. Decoded/presented FPS, local decoder queue depth/oldest age, drops, worker stage/elapsed time, recovery-IDR state and decode/present processing costs belong only to the local viewer receiving that source. Sharers have no remote viewer decoder report. |
| Participants | Exact authenticated room account/device/participation matched against current roster account membership and native sourceKey | Expand/select each voice participant, sharer or viewer for its identity, room, state, rates, safe failure reason and endpoint details. Unknown route state remains unknown even if the participant is online or joined. |
| Network | Existing control event-channel status, group voice/screen route flags, local native generation, existing screen selected_ice_pair/security | Discovery/control TLS is separated from each WebRTC UDP/TURN media route. Payload submission/receipt Mbps is not encrypted network-wire bitrate. Room epoch, local participation/share and session generation remain visible; unexported route IDs/generations remain N/A. |
| Graphs | Existing native counters/rates, separated voice and screen, plus AU/decode/present FPS when exactly one local viewer is selected | At most 120 in-memory points, about one observation/sec. FPS for multiple unrelated local viewers is not averaged or substituted. Pause stops graph sampling only. No additional timer, native polling or telemetry database writes. |
| Events/export | Observed room/participant/service state, participation, generation and recovery changes | Up to 64 transition entries in RAM, UTC observation timestamps. They are observed transitions, not reconstructed signaling-stage durations or a historical session catalog. Existing privacy filtering is strengthened with the shared credential-safe text filter. |

## Ownership and lifetime protections

The existing service snapshot publication now includes immutable metadata captured at the same native read: account/device, group/room/epoch, local participation, share identity, profile/discovery generation and local native session generation. Screen retirement clears this observation; voice stop clears its observation. Getters remain nonblocking cached reads, with no native calls on the WPF thread.

Projection accepts only a current authenticated room/roster mapping, matching local account/device and profile/authentication lifetime, epoch, local participation and sharer role/share identity. Snapshots older than four seconds become unavailable. Native routes are filtered against exact current authenticated device participation *before* aggregate rates are calculated. A leave/rejoin therefore cannot inherit the previous participation's metrics.

Graph history resets on its exact group/room/epoch/session owner change. Event history is cleared for a different profile/authentication/group owner. Participant selection includes room, participation and local native generation; a changed or removed selection cannot borrow another participant's detail. Window context uses the local-to-authenticated group mapping plus an explicit room-ID namespace, not names/timestamps. Each existing room control has Open Diagnostics. The group header continues following the current chat while the detachable diagnostics window can retain a different selected group/room. Scroll position and current participant expansion survive refresh where the owner remains valid.

The private fixture provider is null in normal operation. Production does not use sample data. Screenshots are labeled SAMPLE FIXTURE — NOT PHYSICAL ACCEPTANCE.

## Missing / unavailable inventory

- Per-participant NetEq jitter-buffer delay, concealment, packet discard and decode/mix cost exist deeper in native voice components but are not exported by the current group bridge. They remain N/A; DM/local global receive statistics are not substituted.
- Native participant route IDs and route generations are not exported by current group snapshot JSON. Local native session generation and authenticated participation/share identity are shown separately. Existing native rate samplers are internally keyed by native route identity; this view does not invent a route ID.
- Voice selected ICE-pair address/state details beyond the current connected/TURN flags are not exported. Screen endpoints expose their selected pair and DTLS-SRTP detail; unavailable values remain N/A.
- RTT, packet-loss percentage, congestion, remote bandwidth, wire bitrate, end-to-end audio/video latency and remote processing/playback confirmation are unavailable.
- Shared group capture/encode processing timing and shared encoder queue depth are not exported. Per-viewer sender endpoint data must not be mistaken for shared encoder costs. API submission FPS remains per endpoint, not a claimed overall wire-frame rate.
- Authoritative join/leave and current service errors are observable. Full signaling timestamps, offer/answer forwarding timings, first-packet-to-first-frame correlated intervals and historic room/session records are not retained by this view.

## Delayed second viewer

No delayed-viewer fix or optimization was made. Current evidence can distinguish authorized participation with no observed route; Awaiting offer/Connecting; connected transport awaiting media; received RTP awaiting a complete access unit; accepted access units awaiting decode; decoded frames without observed presentation; locally presented frames; and recovery-IDR/error state. The latter stages use existing cumulative counts plus current FPS, not fabricated historical timestamps. A sharer cannot inspect a remote viewer's decoder without separately supplied remote telemetry.

The earlier approximately 30-second second-viewer delay cannot be assigned a cause from these fixtures. A physical capture of both sharer and delayed viewer remains necessary; the missing correlated signaling/first-frame timing would be a separately scoped instrumentation task.

## Validation

- Focused GroupCallsProjection and packaged WPF tests: simultaneous voice/screen, one sharer/two viewers, correct source-only local viewer metrics, AU FPS versus RTP packets, recovery state, stale epoch/profile/discovery/native session/participation, room filtering, membership removal, privacy, bounded samples/events, expansion retention, pause, seven-tab navigation, resize and 125/150/200% layout simulation: passed.
- Existing group voice session test: one-member room, one encode/fanout, framing, incremental authorization, mute/deafen, retired callbacks and 30 lifetimes: passed.
- Existing screen session/topology tests: 1/2/3 viewers, capture-once/encode-once, incremental viewers, failure/removal, stale participation/revision, stop/restart, production TURN-only/credential/endpoint/generation guards and 15 lifetimes: passed.
- Existing real host shared-fanout test: one Windows capture/software encoder and two real RTP/MFT receivers, capture bootstrap renegotiation, removal preserving the remaining route; observed 18/8 presented frames. This is a host pipeline check, NOT multi-PC/Pi/TURN physical acceptance or sustained FPS.
- Authenticated room tests: three real isolated TLS event channels, independent rooms/viewers, disconnect/reconnect, removal, room-bound signal forwarding and denial, stop/rejoin generations, close revocation: passed.
- Existing native signed device-binding, account/epoch/idempotency/revocation/ordinary-member approval security tests and group registration tests: passed.
- Existing Connections, File Transfers, DM, media lifecycle, attachments, History Settings and group surface regressions, plus Windows native dependency/load/package audit: passed after correcting a cross-section tab-switch ordering regression found by the full suite.

Projection overhead: the final isolated four-route simultaneous-media fixture took approximately 256 ms for 200 fixture construction + projection reads (about 1.28 ms each). This includes fixture setup but excludes native work and full WPF rendering/live-media contention. The UI reuses cached reads and runs about once/sec, bounded to 120 points/64 events, without database writes. No claim of zero overhead or media performance improvement is made. Actual multi-PC CPU/continuity overhead remains a physical acceptance check.

Existing compiler VersionInfo/unawaited-code warnings remain; no new native build is needed. No server deployment, APK, data migration or profile modification was performed.

Final regression artifacts: `build/tests/media-performance-78a8fffa60ad4c7b92da04713f9168d7` (the full suite also runs Group Calls fixtures); focused Group UI/screenshots: `build/tests/group-calls-diagnostics-ce44310d8041400fb67d61b0a2076fab`. The full-suite Group projection benchmark measured 209 ms/200 reads; the separate focused run measured 256 ms/200 reads. These are fixture/projection costs, not isolated live-call overhead. Group security artifacts include `build/tests/group-device-ca3f6041d092473396c1d036bd967d57` and `build/tests/group-rooms-01e77cd16aee4284a44769d81600de75`.

Windows test package: `build/development/4.0.2-dev.143/PeerChat-4.0.2-dev.143-Windows.zip`; extracted launch executable: `build/development/4.0.2-dev.143/Client/PeerChat.Desktop.exe`. Reviewed fixture screenshots are in the adjacent `Screenshots` directory: Group-Overview.png, Group-Voice.png, Group-ScreenShare.png, Group-Participants.png and Group-Network.png. The package contains runtime files and this report, not test fixtures, user profiles or credentials.

## Physical acceptance checklist / mandatory stop

1. Open dev.143 with existing profiles. Join group voice on multiple clients; open Group Diagnostics from the header and from a specific room. Verify group/room/device identities and that local AEC is not shown as remote AEC.
2. Share one screen to two viewers, with voice still active. Inspect the sharer and each viewer: capture/encode/API FPS versus complete received AU/decode/present FPS, payload rate semantics, queue/recovery state and truthful N/A fields.
3. Have a viewer leave/rejoin or reconnect; switch rooms; stop/restart sharing. Old participation/session metrics must not carry into the new owner. If startup is slow, export both sharer and affected viewer diagnostics; do not treat this build as a delay fix.
4. Expand participants, scroll, resize, pause/resume graphs and switch all seven Group tabs plus other primary sections. Open/close diagnostics during media; it must not reconnect, interrupt or stop the call/share.
5. Check Copy/Export privacy and existing Connections, File Transfers and DM diagnostics. Dev.141 transfer and dev.142 DM physical acceptance remain separately pending unless the user reports acceptance.

Group Calls multi-PC physical acceptance: PENDING. Stop here for the user's results; do not start History Sync, crash reports, the broader UI rebuild, transfer optimization or a 5 GB limit change.
