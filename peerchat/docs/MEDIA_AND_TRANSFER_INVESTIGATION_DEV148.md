# DM screen pacing and concurrent transfer investigation — dev.148

Base: dev.147 / `68b0616`. Windows-only candidate. Physical acceptance **pending**.
Independent mailbox error-domain checkpoint: `4de662d`.

## What changed

- The existing Windows H.264 viewer requests optional Media Foundation `MF_LOW_LATENCY` before starting streaming. A rejected request leaves the previous transform behavior intact; diagnostics expose whether the property was accepted. This does not confirm hardware acceleration or remove H.264 reference/recovery validation.
- Fixed-size local observers measure complete access-unit arrival, decoded output, and successful `Present` return gaps. Each keeps 120 gaps, lifetime mean/maximum/count and gaps exceeding 1.5 configured frame periods. Recent p95 uses the last 120 gaps. No media bytes, unbounded event history, continuous database writes, or new worker is introduced.
- MFT input/output calls, presentation setup and swap calls now have separate bounded timing. Existing combined decode/present and queue-age measurements remain.
- Received, decoded and presented FPS now use the same native rolling steady-clock window (approximately 0.5–2 seconds). The managed viewer prefers that decoded rate, retaining a legacy counter-delta fallback. The prior UI counter delta and native rolling presented rate were different sampling boundaries; 8.9 versus 29.77 did not establish a corresponding lifetime frame loss.
- Local mailbox-operation failures retain their rejection and caller exception but have a dedicated diagnostic owner and operation number. They no longer generate a selected-friend `reliable_transport` failure. Genuine socket/TLS/protocol failures remain connection failures.

No capture/encoder rewrite, swap pacing change, queue enlargement, higher bitrate, file protocol/window/size change, Android update, Pi modification/deployment, or profile/grant/history migration occurred. Native Windows and saved backend selections remain available and unchanged. The dev.147 sender-journal optimization is preserved.

Microsoft documents the low-latency property for real-time codecs, including its possible quality tradeoff: [MF_LOW_LATENCY](https://learn.microsoft.com/en-us/windows/win32/medfound/mf-low-latency), [H.264 decoder property](https://learn.microsoft.com/en-us/windows/win32/medfound/codecapi-avlowlatencymode).

## Actual viewer path and measurement boundaries

Windows Graphics Capture callback → phase-preserving capture admission → single pending captured frame → existing software/hardware-selected H.264 encoder → Annex-B access units → WebRTC RTP/ICE/DTLS-SRTP → validated/reassembled complete access units → bounded eight-entry decoder queue → synchronous D3D11-aware MFT input/output → video-processor blit → `Present(1,0)` on the decoder worker.

The reported physical H.264 flow uses TURN/UDP, not PeerChat's TCP/TLS file relay. No TCP changes were applied to UDP. The controlled local fixtures use host UDP ICE and cannot establish physical TURN pacing.

D3D11 hardware device creation, device-manager association and multithread protection are verified by the existing receiver. They do **not** prove that the decoder uses a hardware video engine. Hardware acceleration remains explicitly unconfirmed; no GPU/driver utilization is fabricated. The chosen synchronous transform may use DXVA through that manager.

Arrival gaps end at admission to the decoder, after RTP assembly/security/recovery validation. Decoded gaps mark actual MFT output. Presentation gaps mark successful API return, not physical monitor scanout. Idle is included and is not misclassified as proven packet loss or a freeze. Queue age measures local queued residence, not end-to-end capture latency. MFT-output costs include `NEED_MORE_INPUT` calls; operation counts are not all decoded frames.

## Before/after evidence

The unchanged viewer initially buffered/batched output despite approximately 30 FPS averages. Optional low-latency decoding made output cadence track access-unit arrival much more closely. One capture/encoder serves the multi-viewer fixture. Window text changes are the motion stimulus; this is not a physical video-quality test.

Each regular run lasts about 12 seconds, with four seconds warm-up and approximately eight seconds for FPS/process CPU deltas. Gap lifetime counts include warm-up; p95 is recent 120 gaps. Setup, decoder and swap peaks are lifetime observations. Runs are sequential, not a randomized physical-PC trial.

| Local fixture | Before | After |
| --- | --- | --- |
| 640×480, one viewer, presented FPS | 29.99 | 30.00 |
| Same, arrival recent p95 | 46.96 ms | 46.93 ms |
| Same, decoded-output recent p95 | 70.38 ms | 46.78 ms |
| Same, presentation-return recent p95 | 70.17 ms | 47.26 ms |
| Same, lifetime presentation gaps >50 ms | 114 | 9 |
| 1920×1080, one viewer, presented FPS | 29.89 | 29.95 |
| Same, arrival recent p95 | 47.37 ms | 46.92 ms |
| Same, decoded-output recent p95 | 70.88 ms | 46.98 ms |
| Same, presentation-return recent p95 | 66.18 ms | 46.44 ms |
| Same, lifetime presentation gaps >50 ms | 138 | 4 |
| Same, presentation-return lifetime maximum | 83.51 ms | 52.79 ms |

With three local viewers, recent presentation p95 changed from approximately 71.8/72.2/71.0 ms to 48.2/48.7/47.7 ms, with approximately 30 FPS and no refused decoder queue entries. Preserve raw logs as authoritative for per-viewer rounding.

Do not overclaim: the 640×480 candidate had a **132.59 ms isolated presentation-gap peak**, higher than the baseline 94.57 ms, despite improved p95. The 1080p process CPU delta increased from 6969 to 8281 ms over approximately eight seconds. This is whole-fixture CPU (capture, encoding, forwarding, decoding/presentation), not decoder-thread CPU or GPU utilization. No CPU improvement is claimed. Swap calls were short here (roughly 0.15 ms mean), but the physical 192–239 ms presentation peak was not reproduced or explained away.

Evidence: `build/tests/dev148-screen-{baseline,instrumented,lowlatency}.log`, `dev148-hd-{before,after}.log`.

## Static screen / motion responsiveness

The current capture path is callback-driven, with no timer that manufactures repeated frames, and resumes admission on the first eligible callback after a long pause. Nevertheless, this Windows 10 build 19045 host delivered/admitted **120 frames during four seconds without fixture repainting**. A callback is not proof of changed pixels. The existing adapter cannot safely infer unchanged content from the callback alone.

An active/static/active 18-second run measured resumed-motion capture/encode/present rates around **29.97 FPS** over its final eight seconds. First observed new encoded output was 15.56 ms after motion resumed; first observed presentation-counter increase was 31.27 ms. These are local counter observations, **not pixel-correlated first-new-image or end-to-end latency**. Existing keyframe requests remained enabled.

Newer Windows exposes [capture dirty regions](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframe.dirtyregions?view=winrt-26100), but that API is not available on this build 19045 validation host. No untested Windows-11-only suppression or expensive GPU→CPU pixel readback was shipped. Unchanged-frame suppression remains a known limitation on this path, not a claimed idle-FPS improvement. Cursor movement, scrolling and real video responsiveness require physical acceptance.

## Screen stop, mailbox error and reliable ownership

`Invalid mailbox protocol object` is a mailbox validation exception, not a diagnosis of TCP framing corruption. The old local command catch emitted the same generic Error event as a transport failure, without an attempt owner. The desktop attributed such an ownerless error to the selected friend and retained it as `reliable_transport`. This misleading diagnostic classification is proven and corrected; the original rejecting predicate in the physical event is not known from that old message alone.

The local mailbox caller still fails closed. New events contain operation 0–8 and `mailbox-control:operation`, with attempt zero; malformed peer frames and real attempt failures cannot acquire this owner through an authenticated device identity. Genuine connection failures are not filtered by text matching.

Screen teardown stops the screen media backend and clears only its share lifetime. Active file transfers retain independent route demand, including suspended nonterminal transfers. The native service refreshes file demand before idle retirement. The mailbox task's error continuation schedules its existing retry; it does not close discovery, peer TLS, file state or the main login.

A real Node/Schannel + WebRTC fixture passed: an active 10 MiB file, a deliberately malformed mailbox command producing the exact exception, screen-media stop, unchanged reliable owner and exact final content. Independent opaque-relay file tests passed authenticated restart/reconnect, durable-prefix resume and final integrity. These are isolated host fixtures, not live Pi logs.

The additional opaque-relay concurrent scenario passed: screen stop left the active file's authenticated route intact; a subsequent deliberately injected interruption retired both attachments; file demand created fresh attempt IDs, mutual TLS reauthenticated, confirmed-prefix resume completed, and all 10 MiB matched exactly. The fixture forwards opaque peer TLS bytes locally and uses test-shortened reconnect backoff; it does not implement Pi sockets/outer TLS or prove production retry duration. Repeated share start/stop/decline also passed afterward.

The supplied `PeerChat-diagnostics.txt` is a **group viewer recovery** snapshot, awaiting IDR with zero current complete-frame output despite RTP byte reception. It is not the earlier DM teardown failure. Recovery validation remains intact; receipt of RTP bytes is not displayed as decoded frames.

## Why 42–44 Mbps was not a physical packaged-client result

The dev.147 32 MiB fixture used two C++ Node workers in one Windows process, real mutual Schannel peer TLS, real read/hash/DPAPI journals, receiver flush-before-ACK and verified atomic completion. The account/friend snapshot and relay-ready pairing were supplied by the isolated test. Two independent blocking forwarding threads copied opaque TLS output to the other Node's input, up to 64 KiB per output batch, waiting up to 50 ms only when empty. The zero-delay cases inserted **no network delay**. They took 6.087–6.316 seconds from receiver acceptance to observed completion, with 250 ms completion-observer quantization.

There were no separate physical endpoints, LAN sockets, Pi process, discovery login/`relay_attach`, production managed `RelayTransport`, outer server-facing TLS, session-prefix framing, router/NAT or TURN in that throughput figure. A real encrypted peer/file pipeline was measured, but not the physical deployment. The test's short-backoff flag is test-only and does not alter steady-state window/ACK rules.

The old packaged WPF host fixture instead forwarded data/ACKs through its observer loop with `Task.Delay(5)`, UI dispatcher/diagnostic work and Windows scheduling. It was not a physical LAN fixture either. Dev.148 repeats produced **5.56/5.38/5.50/5.54 Mbps** closed/open/open/closed. That numerical resemblance does not identify the cause of the physical 5 Mbps.

A new controlled **same packaged managed/native boundary**, real peer TLS and real durable 10 MiB file comparison measured:

| Host forwarding mode | Payload Mbps | Mean ACK turnaround | ACK→next submission |
| --- | --- | --- | --- |
| Observer-driven 5 ms polling | 5.36 | 40.40 ms | 8.04 ms |
| Two independent blocking managed forwarders | 27.58 | 5.16 ms | 4.10 ms |

Both final files verified exactly. This demonstrates substantial host-fixture scheduling overhead; it does not prove a corresponding production defect. Production `RelayTransport.WriteLoop` already blocks on native output and reads its authenticated framed outer TLS independently of WPF polling. Its TCP_NODELAY behavior is preserved, not newly tuned. The blocking managed fixture still bypasses sockets/Pi/outer TLS; 27.58 versus native 42–44 is not an apples-to-apples regression (different size/run/storage cost/managed observation boundaries). Journal error 5 occurred and recovered in one case; its external cause is not assumed.

Physical ACK turnaround of **30–36 ms** with one outstanding **32 KiB** chunk implies an ACK-only ceiling around **8.7–7.3 Mbps**, before sender processing/idle, other delays and final completion. Roughly 5 Mbps corresponds to about 52.4 ms per full chunk cycle. The remaining 16–22 ms relative to those ACK means cannot be assigned to the Pi, Windows, disk or network without the matching physical ACK→submission/journal/receiver observations. ACK turnaround already includes remote processing and local dispatch; **it is not RTT**. No window or protocol change was made to hide that limitation.

Evidence: `dev148-managed-transfer-scheduling.log`, `dev148-packaged-diagnostics.log`; prior native raw ABBA logs/CSV remain in `phase1-final-comparison/`.

## Validation and acceptance boundary

Passed: Windows build and restricted runtime/ABI/dependency audit; bounded timing/reset/concurrent observers; common-window and legacy DM diagnostics; pure PCM/audio lifecycle; active bidirectional Opus with Compatibility screen startup; group capture-once encode-once/two actual receivers, viewer removal and 15 topology lifetimes; native/Compatibility/mixed backend RTP/decode lifetimes; mailbox domain/UI/account/restart checks; verified concurrent screen-stop/direct-file completion; actual native relay transfer restart/resume/integrity; relay demand/idle/revocation and physical-adapter socket retirement; group voice lifecycle; packaged Connections/File/DM/Group/History/Logs navigation/read-only/privacy; transfer corruption/cancel/source change/restart/atomic completion; 25 committed archive fixture imports, three preserved unresolved records and five worker restarts.

Test harness source-list omissions, a test-only string-type typo and an invalid fixture saved-port setting were corrected, then validation rerun without weakening profile validation. No failed artifact is packaged. Benchmarks do not claim physical voice quality, screen smoothness, TURN quality or LAN throughput acceptance.

## Physical checklist — stop here

1. Run dev.148 on both Windows PCs with their existing profiles and saved backend preferences. Do not change the phone APK or Pi configuration.
2. During an active DM voice call, share a static screen, then move the cursor, scroll and play video. Capture received/decoded/presented FPS and all three gap metrics, MFT/setup/swap timings, queue age and recovery state. Note visible stutters separately from idle FPS.
3. Send a 10–32 MiB file through the Pi relay while sharing. Stop the share while payload is active; verify the file finishes exactly and messaging/call remain functional. Copy Connections events/full attempt IDs and selected transfer Pipeline/Receiver diagnostics before/after stop. A genuine disconnect still requires its actual close event, not only a mailbox operation label.
4. Compare the same file with diagnostics closed/open; retain confirmed bytes, elapsed time, mean/max ACK, ACK→submission gap, journal/read/write/flush costs, route establishment and first payload delays. Verify resume if interruption occurs; do not clear profiles or identities.
5. Briefly verify existing group viewing/recovery. Do not treat a transport connection or incoming RTP byte rate as proof of presented frames.

**STOP for physical acceptance.** No next milestone, live server update, Android change, file-size/window increase, or broader capture/media redesign is included.
