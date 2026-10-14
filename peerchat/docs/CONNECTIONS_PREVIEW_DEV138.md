# Development 138 — Connections UI-only acceptance

Extends the existing unified Diagnostics & Graphs window with a fifth primary tab. No new window, backend collector, retention owner, protocol behavior or persistent telemetry is introduced. The original Connection & Diagnostics window and its history, failures, raw text, copy/export and live-refresh paths are unchanged.

## Interface

Connections secondary tabs: Overview, Sessions, Network, Failures, Events and Raw Logs. Overview uses compact status/discovery/peer/route/recovery cards. Sessions distinguish established connections, reconnect attempts and completed historical sessions; selection reveals stable sample IDs, contact/group, UTC start, duration, outcome, route and attempt correlation. Failure selection exposes reason, stage, association, time, ID and recovery in an expandable card. Events filter by severity, type and session and retain chronological order with correlated attempts. Logs support line search, readable/structured presentation, explicit redaction markers and sample-only copy/export.

The independent typed `IConnectionPreviewProvider` supplies fixed in-memory fixtures only. It does not reference native adapters, discovery clients, profile storage, real retained events or live account data. Its states are Connected, Connecting, Disconnected, Authentication failed, Relay connection failed, Reconnecting, Recovered, Historical completed session, Empty, Loading and Unavailable. The latter three are visual states, not pending collection jobs. All pages carry the SAMPLE CONNECTIONS PREVIEW banner. Unknown measurements (including RTT, bandwidth, retransmissions and WebRTC media candidates) are N/A, not inferred from control TLS. Network graphs use a compact unavailable presentation.

Both shared footer exports and Raw Logs exports are unmistakably sample reports. Files default to PeerChat-SAMPLE-connections.txt; exported/copied content begins with the sample banner even when searched. User-requested text export is the only new filesystem write. Preview filters and selection are ephemeral UI preferences, not connection history. Closing releases the preview controls/snapshot. No crash-report or application-log collector is implemented.

## Validation

Windows Desktop/Updater compile as 4.0.2-dev.138; native runtimes remain byte-identical to dev.137. Automated WPF coverage includes all five primary sections, six Connections tabs, eleven sample states, session/failure selection, expansion, rendered timeline rows, severity/type/session filters, searched/structured log output and sample export marking. The reused window is tested at 540×420 and 1400×900 with 125/150/200-percent layout-transform simulations and keyboard navigation. These are host simulations, not physical display acceptance.

Existing group diagnostics/context invalidation, actual original Connection & Diagnostics live refresh, media/audio lifecycle, attachment, History Settings and group-surface regressions passed. Package audits cover required crypto/media ABI, restricted DLL loading and runtime dependencies. No Android, Pi, native protocol or profile data changes.

Rendered Screenshots/Connections-Overview.png, Connections-Sessions.png, Connections-Network.png, Connections-Failures.png, Connections-Events.png and Connections-RawLogs.png are sample UI fixtures, not evidence about real networking.

## Visual review checklist — then stop

Open the unified window through an existing Open Diagnostics entry point and choose Connections. Inspect every secondary tab; try each sample state, select a session/failure, expand details, filter events, search logs and switch readable/structured mode. Verify sample labels remain obvious, N/A is honest, scrolling/resizing/scaling work, and copy/export clearly produce sample data. Check live Group Calls and the separate original Connection & Diagnostics window still work. Await visual approval before backend integration or diagnostics migration.
