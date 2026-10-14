# Direct Message Call Diagnostics — dev.142

Starting point: dfe6a3c / dev.141. Windows-only passive integration in the approved unified window. Connections, File Transfers and Group Calls remain intact. No native instrumentation, media/protocol/codec changes, optimization, database writes, Android/Pi changes or deployments. Native runtime DLLs are byte-identical to dev.141; the 256 MiB file limit and File Too Large preflight remain unchanged.

## Real measurement coverage

| View | Connected existing source | Units / ownership / semantics |
|---|---|---|
| Overview | Managed authenticated call/share IDs, lifetime, peer, state; CaptureVoiceDiagnosticsData start timestamp; existing native voice/screen snapshots | Exact selected logical session, not contact-name selection. Voice elapsed time is since the already-observed call-start timestamp, including setup; screen-only start timestamp is not retained. |
| Voice codec/state | VoiceDiagnostics codec, capture/encoded/submitted/playback frames, input/output availability, capture stage, worker state, device starts/input generation, native active device names; UI selected preferences/mute/deafen | Local capture/playback only. Selected preferences are explicitly distinct from native active devices. Playout includes PLC; it is not a separate successful-Opus-decoding frame count. |
| Voice throughput | opus_encoded_bytes and rtp_observed_bytes deltas at approximately one-second observation intervals | Local encoded Opus Mbps includes encoding before/failed send submission. Incoming observed RTP Mbps includes RTP headers and accepted-source observations, not playout confirmation or encrypted wire bytes. No combined wire bitrate. |
| Voice health | Existing NetEq current/target delay, arrival jitter, concealment events/samples, discarded packets/buffer flushes, playback-all-buffers-returned events, receive busy-or-silence blocks | ms and counts, locally observed receive state. Busy/silence blocks are not proven audio underruns; RTP missing estimates are not measured packet-loss percentages. |
| AEC/processing | Existing AEC enable/reference state, delay estimate, reference underruns, render blocks, resets, failure; perf_opus_encode, perf_apm_capture, perf_receive_pull | Existing cumulative count/last/max/mean/deadline-miss fields in microseconds. Receive-pull cost includes NetEq decode/mix, not separate decoder timing. |
| Screen sharer | ScreenShareDiagnostics capture_fps, encode_fps, transmitted_fps_send_api, effective_encoded_mbps_send_api, encoder/backend/codec, requested_fps, queues/drops, perf_screen_capture/input/output | Local capture, encoding and API acceptance; not transmitted wire FPS or delivery assurance. Remote viewer costs are not assigned to the sharer. |
| Screen viewer | received_fps_access_units, rendered_fps, effective_received_mbps_rtp_callback, decoded_frames deltas, decoder queue/oldest age, perf_screen_decode_and_present/present, queue drops and recovery-IDR state | Received complete access units are frames, not RTP packets. Presented FPS is native rendered FPS. Observed decoded FPS uses generation-bound counter deltas. Oldest queue age is not end-to-end frame age. |
| Network | Separate voice and screen selected ICE pairs, ICE/gathering/signaling/DTLS-SRTP state, media session ID, media policy, screen transport, logical recovery state/generations | Independent WebRTC media owners. Current authenticated peer-control TLS metadata and recorded control ID at call start are separate; neither is substituted as a media route. |
| Events | Observed exact-session state/generation, device/continuity/error changes; existing native 16-event anomaly rings | New view stores at most 64 observed transitions in RAM. Observed UTC is not reconstructed establishment time; native anomaly steady_ms/age_ms remains monotonic, not fabricated UTC. No historical call catalog. |
| Graphs | Observed encoded-Opus/received-RTP rates and capture/encode/received-access-unit/presented FPS | Up to 300 points, approximately once per second, while the DM view is visible. No pre-open samples. Pause stops graph collection only; current snapshots and call continue. |
| Settings | Existing text size/compact-card controls plus local graph range and advanced audio expansion | Window presentation only; no volume/AEC/backend/network/pacing changes. |

## Scope and lifetime safety

The selected identity contains profile/generation, local device, account/authentication-session lifetime, exact call/share ID, peer and logical lifetime. Screen entry points use an explicit share namespace, so even colliding call/share strings cannot select the wrong owner. Voice and screen metrics appear together only for the same peer and matching individual authenticated owner bindings.

Voice raw snapshots must match receive_call_id, receive_call_lifetime, receive_media_generation and receive_remote_peer_id, with live negotiating/connected media state in both the managed and native observations. Screen raw snapshots must match share_id, expected_peer_id, share_lifetime and media_generation; retained stopped snapshots are not presented as active during recovery. Already-retained snapshots can otherwise contain connected track/ICE data after an engine has stopped.

Rates are keyed by the exact native media generation/session as well as the logical call. A generation change resets baselines; a fresh read cannot reuse the previous generation's rate before the next observer tick. Counter regressions produce Waiting for samples, not a negative/fabricated rate. Zero counter movement produces zero. Logical recovery state remains visible even when there is no current engine snapshot.

Friend snapshot revision changes on ordinary metadata refresh. It is not an authentication-session lifetime: DM diagnostics uses discoverySessionGeneration for the latter, so refresh/reconnect without session replacement does not invalidate an otherwise selected call. True profile/account/authentication replacement clears data; opening a new call does not silently replace an explicitly selected old session. An initially empty DM view may adopt the first actual active session. Window close unsubscribes the shared timer and clears RAM history/callbacks. Collector exceptions become unavailable UI state, not media control, disconnect or logout.

## Missing / not retained

- Configured Opus bitrate is not exposed by the existing snapshot. Encoded bitrate is shown separately, not used as a claimed configured target.
- RTT, packet-loss percentage, congestion, encrypted wire bitrate/transport bytes and remote audio/device/processing measurements are not available through the pinned ABI with safe ownership.
- End-to-end audio capture-to-playout latency, separate successful Opus decode count/cost, complete end-to-end video frame age and end-to-end encode latency are not instrumented. Existing local stage costs, buffer delay and queue age remain distinct.
- Screen-only elapsed duration and a historical completed-session catalog are not retained. Retained failure diagnostics remain available in the original diagnostics implementation; no unrelated events are converted into past sessions here.
- Selected media state can legitimately be Waiting for samples or unavailable while engine generations change. Diagnostics does not keep a stopped engine alive or bypass identity/generation checks to fill fields.

## Validation

- Existing media/Connections/Group/unified UI suite, including isolated mutual-TLS messaging and real file-transfer checks: passed.
- Focused DM projection and packaged WPF suite: voice-only, share-only, combined, sender/viewer ownership; wrong peer/profile/lifetime/generation; stopped-engine recovery; stale-rate prevention; duplicate call/share IDs; metadata refresh versus authentication replacement; idle-to-active update; session switching; pause/resume; bounded memory; privacy; window resize and 125/150/200% layout simulations; cleanup and native idle-provider isolation: passed.
- Existing host Compatibility-with-voice test: bidirectional injected PCM crossed actual Opus/ICE/DTLS-SRTP while real Windows H.264 capture crossed ICE/DTLS-SRTP into decoder/presentation. The successful run observed 17 encoded submissions, 124 received RTP packets, 17 complete access units, 2 decoded frames and 2 presented frames. This is a startup/continuity check, NOT sustained-FPS, microphone quality, Pi/TURN or multi-PC physical acceptance.
- Passive projection benchmark: 200 active fixtures averaged approximately 2.9 ms per projection. This excludes native snapshot reads and WPF/media contention, so it is not a claim of zero total live-call overhead.
- Package/native dependency and load audits, attachment, History Settings and Group surface regressions: passed. Existing compiler VersionInfo/unawaited-code warnings remain.

Screenshots use explicitly labeled SAMPLE FIXTURE data for deterministic UI coverage. Production has no mock provider in normal operation. Developer layout preview is separately enabled only by PEERCHAT_DIAGNOSTICS_DM_PREVIEW=1. The private test input boundary is null in production. Physical acceptance is pending.

Final full regression artifacts: build/tests/media-performance-de51827dc021408fbc19651234da9672. Focused final DM artifacts: build/tests/dm-diagnostics-f8d59a61d6984315b297eb26424a6869. Existing actual host media startup/continuity artifacts: build/tests/screen-compat-voice-2c4e4bc208f24c9a99f50827c65f399a. Final full-suite active-fixture projection mean was 2.812 ms over 200 reads. Native pipeline costs and live-call UI overhead are not isolated by that benchmark.

Windows package: build/development/4.0.2-dev.142/PeerChat-4.0.2-dev.142-Windows.zip, 5,842,992 bytes; SHA-256 0B3DA0922337A2129C5BE879F0BA2BB11C50A77BD6E85F8E7E20E16ABB5F7611. App constant verified as 4.0.2-dev.142. Reviewed screenshots copied to build/development/4.0.2-dev.142/Screenshots (dm-overview-FIXTURE.png, dm-voice-FIXTURE.png, dm-screen-FIXTURE.png, dm-network-FIXTURE.png). No APK or server update is required.

## Physical checklist / mandatory stop

1. Start a real DM voice call; use Open Diagnostics in voice details. Verify the peer, exact call identity, states and local audio counters; N/A must remain honest.
2. Run screen sharing without voice and use Open Diagnostics in Screen Media. Check the local sharer/viewer perspective and captured/encoded/received-AU/decoded/presented FPS distinction.
3. Run voice and screen together for the same peer; inspect separate control and media routes and their throughput semantics.
4. Pause/resume graphs, resize, switch tabs and close/reopen diagnostics while talking/sharing. Neither call nor viewer should stop or reconnect merely because diagnostics opened.
5. End/restart the call and test a reconnect where convenient. The old selected session must not borrow the new call's measurements; select it explicitly from the new controls. Copy/export a report if any problem occurs.
6. Check existing Connections, File Transfers and Group Calls. Dev.141 file-transfer physical acceptance remains separately pending; do not count this milestone as acceptance of that work.

Stop after this checkpoint/package: no Group/History/Crash Reports integration or optimization is included.
