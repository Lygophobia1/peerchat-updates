# Development 136 — unified Windows diagnostics layout acceptance

Prior shared native diagnostics/History status checkpoint: `1892181` (dev.135). This checkpoint extends the existing WPF Group Diagnostics window; it does not replace its live provider, telemetry collection, bounded graphs/events, pause, copy or export. Existing Connection & Diagnostics and file-bubble statistics remain available.

## Shared shell and provider boundary

One reusable detachable window accepts a typed section and stable transfer/call/history context. Top navigation exposes File Transfers, Direct Message Calls, Group Calls and History Sync. Tab registries and typed pages/panels/metrics permit future providers/sections without backend ownership in the window. Connections and Crash Reports are not implemented. Display preferences (compact cards and text size) are in-memory for the application session, not database telemetry writes.

Group Calls uses the existing `GroupTelemetryProjection` and existing native snapshots. The other three sections use an isolated `IDiagnosticLayoutProvider` implementation with no native, server, storage, authentication or history dependencies. It refuses Group Calls. Every preview carries a prominent SAMPLE banner; illustrative numbers are labeled sample and unconnected fields are N/A. Active/completed/stalled/failed/disconnected/unavailable previews are layout states, not state changes to the selected operation.

File Transfers has Overview, Network, Pipeline, Receiver, Pi Relay, Events and Settings. Pipeline includes chunk index/size/count, maximum/actual concurrency, ACK wait, retransmissions, queues, throughput and collapsible stage timing. Sample chunk values are not configured limits. DM Calls has Overview, Voice, Screen Share, Network, Events and Settings with sharer/viewer cards, AEC/Opus and distinct control/media fields. Group Calls preserves Overview, Screen Share, Voice, Participants, Events and Export; route details are collapsible. History Sync has Overview, Connections, Records and Events, with current activity and prior completion in separate cards.

Repeated screen-share lines were separate native viewer-route summaries, not duplicate display bindings. Each summary now carries a route ordinal and existing route reference. No route measurement is discarded. Overview moves verbose route text into Advanced; empty graphs use compact N/A text. Actual payload rates are not presented as wire bitrate or remote playback confirmation.

## Entry points and scope

Attachment bubbles expose Open Diagnostics using the native stable transfer ID. Voice Details and History Settings expose the same read-only entry point; the group header reuses Diagnostics Graph. Opening or navigating never starts/stops a transfer, call, grant or sync. No protocol, chunk limit, ACK, pacing, media architecture, server or Android implementation changes in dev.136. No Pi deployment or ADB installation.

## Validation

Windows client/updater build passed. Existing media/native scheduling and audio lifecycle tests passed; no physical audio devices opened. WPF tests cover all four sections/tabs, all six sample states, Group Calls sample refusal, reused single-window context, transfer action identity, unchanged call identity, resize at 540×420 and 1400×900, 125/150/200-percent layout-transform simulations, keyboard focus traversal, sample export marking and tab preservation. Rendered previews reviewed for dark contrast and empty states. Attachment, History Settings and group-surface regressions passed. Package audit must confirm unchanged native dependency/crypto/media containment. Display scaling simulations are not physical monitor acceptance.

## Manual visual checklist — then stop

1. Open attachment Open Diagnostics; confirm the selected transfer ID, navigate every File Transfers tab, expand stage timing and view active/stalled/failed/unavailable previews.
2. Open Voice Details → Open Diagnostics; inspect every DM tab and sender/sharer versus receiver/viewer layouts. These views are samples, not new call telemetry.
3. Open a group → Diagnostics Graph; confirm real Group Calls statistics remain live, all six tabs, participant expansion, graphs/pause/resume, copy and export. Check distinct route labels.
4. Open History Settings → Open Diagnostics; inspect all four tabs and clear separation of current and last-result cards. The existing actual History Settings status remains native-backed.
5. Resize, maximize on 1920×1080, test smaller windows and your normal display scaling, scroll, keyboard navigation, text-size and compact controls. Verify sample labels are always obvious.

Physical visual acceptance is pending. Stop for the user's requested layout changes; do not connect new instrumentation until approved.
