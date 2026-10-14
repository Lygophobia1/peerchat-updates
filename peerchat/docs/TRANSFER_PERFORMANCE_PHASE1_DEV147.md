# File transfer performance, Phase 1 — dev.147

Base: dev.146 / `66c7cd0`. Windows-only candidate. Physical acceptance pending.

## Outcome and scope

The demonstrated host bottleneck was redundant sender journal persistence between chunks. Windows previously committed a new durable high-water bound before transmitting each chunk, then separately committed its acknowledged offset. The new Windows adapter coalesces only the second, ACK-only update with the next mandatory pre-send commit.

The 32 KiB chunk, one outstanding chunk, 256 MiB limit, authentication, encryption, receiver flush-before-ACK, SHA-256 completion, atomic publication, wire format and journal version remain unchanged. No Android, shared TransferManager, server, relay protocol, media scheduler or persistent statistics schema was changed. No Pi deployment or device installation occurred. Existing Android background-transfer acceptance remains preserved.

The reported physical foreground/awake Android average of **0.372 Mbps** and physical Windows network average of **2.84 Mbps** are distinct from these host fixtures. The Android root cause is **not proven**. Nothing here attributes it to background scheduling or claims a physical Android improvement.

## Measurements and meaning

Windows now collects bounded, session-only observations on authenticated winning-route dispatch:

| Measurement | Source and meaning |
| --- | --- |
| ACK turnaround, sample count, maximum | Successful chunk submission into peer TLS/output queue → matching validated Resume offset on the same attempt. Includes queue/transport/remote processing/local dispatch; **not network RTT**. Duplicate and wrong-attempt replies do not add a confirmation sample. |
| ACK arrival → next TLS submission | Local inter-chunk interval including ACK processing, read, journal persistence and scheduling. Not pure idle or CPU time. |
| Receiver dispatch | Local Windows TransferManager receive processing, including write, flush, hash and protected journal. Dispatches are not unique-byte counts. |
| Receiver dispatch → ACK submission | Local processing plus subsequent control scheduling. Windows sender views do not pretend to measure Android disk work. |
| TLS submission | Local packet encryption/queue submission cost; not socket delivery or wire bitrate. |
| Selected journal commits/cost/protection/commit | Commits caused by exactly one changed transfer. Cost includes the owner journal's other records. Multi-record mutations are not apportioned or guessed. |
| Owner journal cost, deferrals, last Win32 error | Profile-wide, not selected-file-only. Existing all-transfer journal timer retained. |
| Existing read/hash/write/flush/commit/backpressure | Existing native per-entry counters, cumulative runtime microseconds/ticks. ACK-wait ticks remain ticks, not elapsed ACK wait. |

Timing observations cover at most 64 IDs per profile-worker session. Missing observations remain unavailable; supplemental measurements never authorize a transfer. No live samples are written to SQLite. Missing optional cache/telemetry allocations fall back without invalidating a durable commit.

Code inspection found both Android's 125 ms backend fixed-delay loop and native transfer servicing from relay OUTPUT calls (empty-output polling at 10 ms). Neither interval alone establishes the physical bottleneck. Android fsync/journal and actual transport costs still need phone-side evidence; Android was not modified.

## Controlled results

`tests/measure-transfer-phase1.ps1` runs sequential ABBA zero-delay comparisons plus delayed relay batches, approximately 1 Hz diagnostic sampling, and two simultaneous files. Fixtures use actual Node workers, real mutual peer TLS, real protected journals and final file/SHA-256 validation. Two independent opaque-ciphertext forwarding threads model the relay adapter. They do **not** exercise the Pi, LAN TCP stack, discovery server attachment, outer server TLS or physical Android.

Baseline and optimized fixtures share collectors and code; a test-only compiler flag disables ACK coalescing for the legacy-equivalent baseline. Production has no runtime switch for this flag. Throughput is decimal Mbps from confirmed completed bytes / accepted-to-completion elapsed time. Initial preparation/offer wait is separate; completion observation is quantized by the fixture's 250 ms poll.

| Case | Baseline | Optimized |
| --- | --- | --- |
| 32 MiB, zero injected delay, ABBA | 27.30–29.86 Mbps; 8.99–9.83 s | 42.50–44.10 Mbps; 6.09–6.32 s |
| 10 MiB, zero delay, ABBA | 31.03–31.10 Mbps | 37.91–38.20 Mbps |
| 10 MiB, 10 ms per forwarded batch/direction | 6.21 Mbps | 8.08 Mbps |
| 10 MiB, 35 ms per forwarded batch/direction | 2.54 Mbps | 2.77 Mbps |

Injected batch delay is not a configured or measured RTT: a logical chunk can span multiple TLS output batches.

For 32 MiB, mean throughput increased about **51.5%** across the two pairs. Sole-transfer sender journal commits fell from 2049–2052 to 1028; attributable journal time fell from 4.62–4.70 s to 2.25 s. Mean local ACK-to-next-submission interval fell from 5.18–5.78 ms to 2.30–2.57 ms. ACK turnaround stayed approximately 3 ms and receiver dispatch remained approximately 2.74–2.77 s cumulative. This separates the sender persistence improvement from remote/network wait.

Whole-fixture process CPU (both Node workers, forwarding and observer) fell from 5.08–5.50 CPU-seconds to 3.84–3.97 CPU-seconds; **not sender-thread-only CPU**. Peak process working set was approximately 75.8 MiB in both cases, including fixture payload arrays; not an isolated native-core allocation measurement. No transfer queue/window expansion occurred.

At the larger batch delay, mean ACK turnaround was 87–88 ms and dominated throughput. Increasing a window could address latency sensitivity, but it was **not implemented** without physical classification/compatibility evidence.

## Diagnostics overhead and variability

The final packaged WPF byte-pump fixture recorded 10 MiB closed/open/open/closed results of **5.56 / 5.35 / 5.50 / 5.53 Mbps**, with process CPU **1765.6 / 2734.4 / 2218.8 / 2937.5 ms**. Its observer-driven forwarding is not a production throughput ceiling. The earlier dev.146 equivalent fixture was approximately 4.94–5.14 Mbps; this cross-run comparison is weaker than the controlled native ABBA comparison.

One native diagnostic-sampling case slowed to approximately 16 Mbps, with receiver flush time growing from roughly 0.36 s to 5.40 s over 32 MiB. A recheck sampled 40–48 Mbps with diagnostics and approximately 24–43 Mbps without. Final build activity may have overlapped the last closed recheck. These results are retained, not discarded: host I/O/scheduling variability prevents a zero-overhead or precisely bounded UI-overhead claim. Read-only diagnostic collection adds no journal/database writes or reconnection ownership.

Some baseline/optimized runs encountered journal commit Win32 error **5** and recovered through existing bounded retry behavior. The exact external cause is not proven; no antivirus/permission explanation or timeout fix is asserted. Numeric last-error diagnostics now retain this evidence. ACK timing samples exclude dispatches that fail persistence; full elapsed results include their retry delays.

Raw artifacts: `build/tests/phase1-final-comparison/summary.csv` and case logs; `build/tests/phase1-diagnostics-recheck/summary.csv`; `build/tests/phase1-release-media-diagnostics.log`.

## Safety proof and validation

Only monotonic sender ACK progress at or above the already durable cursor, within an unchanged durable high-water bound, may be deferred. Receiver progress, a lower durable cursor, state/token/identity/digest/expiry changes, new transmission bounds, cancellation and completion force the existing durable write. After a crash in the coalesced interval the sender revalidates its full source, then learns the authoritative durable receiver prefix through authenticated Resume. No new bytes are sent until their bound is committed. Journal encoding remains v1 and old peers still receive the identical protocol.

Validated:

- Real abrupt process death after an ACK-only deferral, receiver durable prefix, restart, exact resumed bytes and final hash.
- Deferral gates, durable rewinds/cancellation/receiver prefixes/completion; source change, missing partial, uncommitted tail, rename crash, DPAPI tamper and profile/device binding.
- Binary/Unicode/empty/1/3/10/32 MiB payloads, duplicate/reconnect/cancel/expiry/peer/limit/corruption checks; actual mutual-TLS file session and message ACK coexistence.
- Ten controlled native benchmark cases, two simultaneous exact-integrity files in each, legacy-equivalent baseline, latency and sampling variants.
- Bounded timing, malformed optional telemetry isolation, wrong-route/offset and duplicate timing rejection, reset, UI attribution/units/unavailable values.
- Completed-statistics deduplication, frozen duration, protected reopen and optional-storage failure isolation.
- Windows build/runtime imports/Go HPKE; six-section Connections/File/DM/Group/History/Logs diagnostics regressions, original diagnostics retained.
- Native audio/voice lifecycle and real group RTP/MFT screen receivers; no physical quality acceptance claimed.
- Authenticated archive security/provenance/cursors/replay, 25 real committed fixture imports with 3 preserved unresolved records, and five actual profile-worker restarts. Real unresolved historical records remain untouched.

Final packaged diagnostics, archive/restart and dependency checks passed using the final rebuilt candidate DLL. Earlier test compiler-guard, manual-link argument and package-layout omissions were corrected before handoff; no failed artifact is delivered.

## Manual physical acceptance

1. Use Windows dev.147 with existing profiles. Keep the existing Android APK; no phone update is needed for the unchanged wire protocol.
2. Send the same representative 10/32 MiB file Windows → Windows and Windows → Android with Android foregrounded and awake. Separate preparation/acceptance wait from payload transfer duration.
3. In the selected bubble's Open Diagnostics, copy Pipeline/Receiver results: ACK sample count/mean/max, ACK-to-next-submission interval, journal costs/errors, confirmed bytes and available local receiver costs. Android remote disk costs must remain N/A on Windows.
4. Compare diagnostics closed/open and verify final file size/hash/content. Check two simultaneous files and one cancel/restart/resume without clearing any data.
5. Confirm existing messaging/calls remain usable. Do not test 5 GB; the limit is still 256 MiB.

STOP after this candidate/checkpoint. Physical Windows network and Android performance acceptance remain pending. No next milestone, Android modification, window redesign or Pi deployment is authorized by this handoff.
