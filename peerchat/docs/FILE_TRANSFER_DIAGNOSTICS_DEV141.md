# File Transfer diagnostics — dev.141

Starting checkpoint: a444bf5 (dev.140). Windows-only integration of existing telemetry; approved unified layout retained. No Android/Pi changes, deployments, protocol changes, limit increases, or new native instrumentation. Native runtime binaries are byte-identical to dev.140.

## Connected measurements and attribution

| View | Real source / semantics | Boundary |
|---|---|---|
| Overview | CoreAdapter.FileTransferData: original name, size, direction, peer, state, done, statisticsBytes, durationMs, rateBits, etaReliable, error | Sender done is acknowledged; receiver done is durable. Submitted payload is never treated as confirmed delivery. Average is confirmed bytes divided by native end-to-end duration, including preparation, waiting and resumed time. |
| Completion | Existing protected optional completed statistics and native terminal state | Missing summaries remain unavailable. Complete means verified completion; no new historical routes or timelines. |
| Network | Existing transfer op11 payload_bytes, route_delay_ms, first_payload_delay_ms, route_changes, backpressure; RateMeter payload rate | Runtime cumulative counts/timings, not wire bitrate. Route changes include initial attachment, not a reconnect total. Zero delay may mean not observed. |
| Associated control owner | Existing ConnectionAttemptTracker authenticated epoch/winner/source/route for exact selected peer | Explicitly NOT the selected transfer's binding or a historical route. No owner from another peer is substituted. |
| Pipeline | TransferManager per-entry read_us, hash_us, write_us, flush_us, commit_us, ack_wait_ticks, last_ack_ms, elapsed_ms; confirmed chunk counts | Current-runtime costs only. ACK-gating ticks include readiness/outstanding-ACK waits; not elapsed ACK latency. last_ack_ms is a monotonic clock reference, not a duration. |
| Owner journal | Existing journal_all_transfers_us in corroborated per-transfer op11 row | Separate advanced subsection, ALL local DM transfers in this profile/account runtime. Never assigned as selected-file cost. Archive rows cannot borrow DM owner counters. |
| Receiver | Local receiving row's durable done, state and processing counters | Sender view cannot claim remote Android disk/flush/commit costs. Completed sender state is an existing peer completion acknowledgment, not a receiver profiling report. |
| Pi Relay | Associated authenticated peer control metadata only | No Pi measurement endpoint, deployment or resource estimate. |
| Events | Existing retained error/state plus bounded state observations while selected view is open | UTC, exact transfer/peer/direction; observed rather than reconstructed offer/accept/retry times. 64 observations max, cleared on context change/close. |
| Graph | Existing native RateMeter snapshots observed approximately once per second | 300 points maximum; no pre-open samples. Stalls/inactive transfers use zero, no retries counted twice. Pause/follow/range/units are presentation-only. |

Read-only protocol facts reflect the unchanged TransferManager: 32,768-byte maximum payload chunk, one outstanding ACK-gated payload chunk per transfer, four active transfers, 256 MiB maximum file size. The maximum is established from implementation, not inferred from visible progress increments. Actual current in-flight depth is not exposed.

## N/A / not retained

- Exact selected-file route/connection ID: current transfer snapshot does not expose its binding. Associated current authenticated peer control owner is shown separately, never promoted into proof of an exact file route.
- Wire bitrate, RTT, packet loss, encryption/decryption/socket-write timing, elapsed ACK latency, retry/timeout totals, queue depth and actual in-flight depth: not exposed by the existing API with safe per-file attribution.
- Remote receiver write/hash/flush/commit costs and receiver queue: no authenticated receiver profiling report exists. Local receiver costs are connected.
- Pi identity, attachment role/pairing ID, RX/TX, CPU/Wi-Fi/queues and worker wake latency: no existing authenticated Pi telemetry source in this client.
- Historical per-stage costs, chunk/ACK graphs and exact lifecycle timeline: not retained. Session counters reset on restart; protected completed average/duration remain available when retained.
- Group recipient fanout: not implemented by this attachment path; no fabricated aggregate or recipient statistics.

## Context, safety and overhead

Open Diagnostics reuses the single unified window and selects a tuple of profile ID/generation, local device, account/authentication generation, native transfer ID, peer, direction and recipient-account reference. Filename is never a lookup key. Missing/ambiguous/stale rows show unavailable, never a different file. Profile/account invalidation applies even with live-follow off. Snapshot age beyond three seconds is not displayed as current telemetry.

The existing once-per-second attachment refresh supplies rows and op11 together; no additional native polling loop, service, thread or database writes. Open diagnostics consumes cached snapshots, maintains only selected-context bounded RAM history and updates controls in place. Window close removes its shared-timer subscription and releases callbacks/history. Text and exports use existing privacy filtering with inline credential redaction; local source/destination paths and contents are not exported.

The protected completion subsystem remains supplemental. Existing isolated statistics tests cover duplicate completion, no live sample persistence, frozen durations, protected reopen, profile isolation and corruption/failure independence. Audit triggers remain test-only.

## Selected-file validation clarification

The native offer predicate already rejects source sizes above 268,435,456 bytes, but an exception/null ABI result previously became the generic managed file-operation error. A too-large offer can fail before any retained transfer row exists, so its original cause is not recoverable through per-transfer diagnostics. The user's historical approximately 800 MB failure was over the limit, but the generic historical report alone cannot prove its exact failing predicate.

At the user's explicit follow-up request, Windows now checks selected-file metadata before operation 1 and displays:

**File Too Large**

“This file exceeds PeerChat's current maximum transfer size of 256 MiB (approximately 268 MB). Please select a smaller file.”

The exact 256 MiB boundary is allowed; one byte above is rejected before native initiation. Inaccessible/missing files still go through normal validation. Native validation remains authoritative for file changes, identity, permissions, integrity and all other security requirements. No limit is raised. A file that grows after preflight may still produce a native validation failure; no bypass or protocol rewrite was added.

## Validation and physical acceptance

Automated suite includes exact identity/direction/account/profile selection; same-filename simultaneous real native offers/cancel; active sender and receiver projections; completed/failed/cancelled/interrupted state presentation; confirmed rates, conservative ETA, stalled zero; owner/transfer separation; bounded history, pause/cleanup; credentials; stale/profile invalidation; presentation units and developer sample isolation. Actual packaged native mutual-TLS transfers verify completed files byte-for-byte.

Windows media/Connections/Group diagnostics, attachment, History Settings, Group surface, protected statistics and package security/load audits are run. WPF checks cover all sections/tabs, navigation, small-window layouts and simulated 125/150/200% scaling. Compilation has the established VersionInfo/unawaited-code warnings; no native ABI or protocol alteration.

Host benchmarks use two isolated real CoreAdapter/native workers and an in-process encrypted relay byte pump, not the Pi, LAN or Android. Each trial transfers the same 10 MiB file; baseline/open/open/baseline ordering. One open trial also captures actual WPF screenshots. These runs cannot establish physical Android performance or identify its bottleneck. Final measurements are recorded in the handoff report below.

| Final packaged-native trial | Accepted → complete | Confirmed payload average | Whole test-process CPU |
|---|---:|---:|---:|
| Closed baseline 1 | 16,417.9 ms | 5.11 Mbps | 2,046.9 ms |
| Open + window creation + screenshots | 17,007.7 ms | 4.93 Mbps | 2,171.9 ms |
| Open, no screenshots | 16,163.0 ms | 5.19 Mbps | 2,453.1 ms |
| Closed baseline 2 | 15,761.9 ms | 5.32 Mbps | 1,828.1 ms |

Mean open elapsed time was about 3.1% above mean closed, including startup/screenshots; the no-screenshot open trial was about 0.5% above mean closed. Mean whole-process CPU differed by 375 ms over roughly 16 seconds; this is not isolated UI CPU. Single-host scheduler/load variance prevents attributing all differences to diagnostics. No claim of zero overhead or Android throughput improvement is made. Every completed destination matched the original 10 MiB bytes exactly.

Final media/diagnostics test artifacts: build/tests/media-performance-7e454153918046e892a2a642c0f8fab6. Reviewed actual WPF captures copied to build/development/4.0.2-dev.141/Screenshots: transfer-overview.png, transfer-network.png, transfer-pipeline.png, transfer-bubble.png. They show real counters from isolated authenticated host transfers, not physical Motorola acceptance.

Windows archive: build/development/4.0.2-dev.141/PeerChat-4.0.2-dev.141-Windows.zip (5,833,684 bytes), SHA-256 76949F38B39185397F366866C75332EA49E8DF1635097D22765FDABA18EA55D7. App constant verified as 4.0.2-dev.141. Final package dependency/load audit, attachment, History Settings and Group surface suites passed after the size-preflight addition; protected statistics suite passed. No new APK or server build is needed.

### Manual Windows → Android checklist

1. Use existing profiles and a file below 256 MiB; no Android replacement build is required.
2. Send/accept, open diagnostics from its Windows bubble and confirm filename/direction/peer/bytes match that exact file.
3. Send two same-named files; each bubble must select its own transfer. Cancel one without affecting the other.
4. Inspect Overview/Network/Pipeline and advanced timing; copy/export the real report during slow progress. N/A fields must remain honest.
5. Pause graphs, change units/range, resize and close/reopen diagnostics; bytes must continue without a new route or transfer restart.
6. Confirm verified completion, final size/duration/average; restart Windows and check retained completed summary. Keep Android's accepted locked-screen behavior unchanged.
7. Select a file over 256 MiB and confirm the exact File Too Large message appears, with no transfer queued.

Physical acceptance for this diagnostics milestone remains pending. Stop here: no optimization, Android changes, DM Calls wiring, further diagnostics milestones or 5 GB limit work.
