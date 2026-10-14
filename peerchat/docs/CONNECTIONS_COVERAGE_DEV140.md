# Development 140 — Connections backend milestone 1

The approved six-tab layout now uses existing real Connections observations by default. The original Connection & Diagnostics window, collectors, log format, transition-only failure retention and live Group Calls remain unchanged. File Transfers, DM Calls and History Sync were inventoried only; their unified-window preview providers were not wired.

## Field coverage

| Category | Fields and boundaries |
|---|---|
| **Connected to real data** | Authenticated discovery event-channel connected/disconnected state; current tracked peer-control winners and latest runtime attempts; exact scoped peer/native epoch/connection-or-attempt IDs; control route/source/candidate/direction, peer TLS/trust and latest progress; existing Connection failure ID/reason/stage/time/domain/operation ID/generation; existing profile-retained metadata events, UTC ordering, logical peer/conversation correlation and filters; redacted current Connection text and existing metadata log; copying/exporting. Network fields follow the selected current diagnostics session, not an unrelated peer. |
| **Missing instrumentation** | Control RTT, bandwidth, retransmissions, packet-loss measurements, wakeup latency and live graphs; lifetime reconnect count. These remain N/A — Not instrumented. A number of stored retry events is not presented as a reconnect total. |
| **Historical data not retained** | Completed connection-session catalog, runtime start times/durations, native session/attempt/demand IDs and epochs in older connection.log records, explicit recovery outcomes, and a dedicated failed-peer association. Retained failures preserve their original IDs, but frozen selected-conversation context is identified as captured context, not proof of which peer failed. No completed sessions are reconstructed from log events or failures. |
| **Existing data not safely integrated here** | WebRTC selected candidates/media statistics belong to independent call owners and cannot safely be attached to a control session by proximity; native transient retry/pending data lacks a passive managed tracker exposure, and consuming ui_poll again would interfere with the application; group/archive authorization-scoped connections are not enumerated by this friend/peer tracker; transition/recovery operation markers are not a completed-session history and have no public passive enumeration API. These are not guessed or newly instrumented. |

Session counts describe distinct **tracked peer-control owners**, not all sockets, group media or archive routes. Failed/lapsed latest attempts are distinguished from established winners. Older events/failures are profile-retained and are not asserted to belong to the current authenticated login. Unknown timestamps/associations and source availability are not fabricated. Network media cards explicitly remain separate from discovery/control TCP/peer TLS.

## Refresh, privacy and developer preview

Passive reads reuse the existing application timer, approximately once per second only while the Connections view is visible. Existing controls are updated in place; selected IDs, filters, expanded detail state, search text, caret and scrolling are preserved. Authentication/profile context changes discard old view state. Closing unsubscribes the refresh and releases snapshots. No new threads, network calls, collectors, continuous persistence, session ownership or retry ownership are introduced.

Redaction occurs before parsing/presentation/export. In addition to existing DiagnosticsPrivacyFilter, the new adapter removes mixed inline token/password/authorization/ICE/TURN credential values and private-key blocks. The new source does not collect messages, payloads, SDP, request bodies or full other-subsystem snapshots. It never copies the discovery session token; cached authentication is a Boolean and is not described as a live TLS connection.

Normal operation has a LIVE CONNECTIONS banner and no simulated-state selector. For developer layout tests only, explicitly start with `PEERCHAT_DIAGNOSTICS_CONNECTIONS_PREVIEW=1`; the isolated sample provider, Preview State label and SAMPLE-marked exports then remain available. This flag does not bind any real collectors to sample state. It must not be set for physical acceptance of live data.

## Validation

Windows Desktop/Updater compile as 4.0.2-dev.140; native crypto/media/core runtimes remain byte-identical to dev.139. An isolated test uses the **actual packaged native CoreAdapter**, authenticated fixture friend bindings, constrained identity pins, genuine mutual peer TLS over the existing managed relay byte adapter, and actual native events feeding ConnectionAttemptTracker. The live projection reports the winner, repeated reads produce no source events/writes, messaging is delivered during diagnostics reads, and relay closure projects disconnected state. This is a host fixture, not a Pi/network-performance or physical-user acceptance claim.

Focused tests pass for real/sampled mode isolation, current winner/attempt changes, scoped session selection, duplicate-owner prevention, no fabricated historical sessions/timestamps/native event correlation, logical filtering and UTC order, retained failure boundaries, inline/PEM redaction, original retention contents unchanged, live controls/search preserved, authentication-context invalidation, and the original Connection & Diagnostics live-refresh regressions. Existing group metrics/window/graph-pause, media/audio lifecycle, attachment, History Settings and group-surface tests pass. Package ABI/dependency containment and restricted native loading are validated.

No production source changes in App connection handlers, ConnectionAttemptTracker, ConnectionDiagnosticLog, DiagnosticsRetentionStore, native protocols, Android or Pi. Existing unrelated object files are preserved untracked.

## Manual acceptance — then stop

1. Launch dev.140 normally with your existing profile. Use an existing Open Diagnostics entry and choose Connections; confirm LIVE, no Preview State selector, and correct discovery/peer status with and without an active peer route.
2. Establish/end a normal peer session while messaging. Confirm scoped IDs/route changes and no reconnection caused by opening/refreshing diagnostics. Select sessions, filter events and inspect explicit historical limitations.
3. Search/copy/export the redacted live logs; confirm full available IDs/UTC, stable selection/scrolling, and no sample numbers. Check retained failures label unknown association/recovery honestly.
4. Verify live Group Calls and the separate original Connection & Diagnostics still work. Resize and use your normal display scaling.

Physical acceptance is pending. Stop before integrating File Transfers, DM Calls or History Sync.
