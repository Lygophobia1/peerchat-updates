# Development 139 — final Connections UI acceptance refinements

The approved interface is retained. Four focused changes:

1. The existing eleven-state selector is labeled **Preview State**, with **Simulated · not live** guidance. Its provider boundary and sample states are unchanged.
2. Compact session/failure/event rows emphasize description, local time, severity/outcome and shortened identifiers. Selection reveals full identifiers in read-only, selectable details; Events has a collapsible technical-detail card. Long descriptions are bounded only in row presentation, never in the underlying model or exports. Session filter keys remain full IDs; abbreviated labels have full-ID tooltips.
3. Rows use the Windows local timezone and the offset applicable to the actual UTC instant, including daylight-saving transitions. Tooltips and selected details retain exact ISO UTC values. Diagnostic exports and readable/structured raw logs retain full IDs and UTC. No sample timestamp was changed. Event sorting still uses UTC DateTime values, not formatted strings.
4. The footer keeps its spacing and Copy/Export/Close. Sample sections label copy/export as sample previews. Pause Graphs remains in place but is disabled with an explanatory tooltip when no applicable live graph samples exist. Existing Group Calls pause/resume, copying, export and graph sampling are preserved; resuming a previously paused active graph remains possible.

Toolbar controls are centered alongside the added preview label. No interface redesign, backend collector, protocol, authentication, persistence, Android or Pi change. The original Connection & Diagnostics window is unchanged. Native runtime binaries are reused byte-for-byte from dev.138.

## Validation

Windows Desktop and Updater compile as 4.0.2-dev.139. Focused tests pass for five primary contexts, all secondary tabs, eleven preview states, session/failure/event selection, event expansion, severity/type/full-ID filters, exact UTC exports, Windows-local default, long descriptions/identifiers, and both Central timezone DST boundaries (spring skipped hour; autumn repeated hour with different offsets). Copyable details retain full text. Group graph pause/resume and disabled-preview footer tooltips are tested. Existing live diagnostics/context invalidation, original Connection & Diagnostics live refresh, media/audio lifecycle, attachment, History Settings and group-surface regressions passed. Package containment validates required native crypto/media ABI and runtime loading.

Minimum-window and 125/150/200-percent layout-transform tests are host simulations, not physical display acceptance. Preview fixtures do not prove real network performance. Connections remains sample-only; backend integration, diagnostics migration and crash collection require a later approved milestone. Windows timezone rules determine local display; exported UTC values remain independent of display formatting.

Screenshots: Connections-Sessions.png, Connections-Events.png (selected event expanded), and FooterControls.png (overview with disabled graph control). All are rendered sample UI fixtures.

## Manual acceptance checklist

- Switch Preview State and confirm simulated-state labeling; navigate all sections/tabs.
- Select sessions/events, hover timestamps, expand details, and verify full IDs plus local/UTC times. Copy selected text or a sample export and verify full identifiers/UTC remain.
- Filter events by severity, type and session. Check long rows, scrolling, smaller windows and your normal Windows scaling.
- Confirm sample footer labeling and disabled Pause Graphs explanation; then verify live Group Calls pause/resume, copy/export, and the separate original Connection & Diagnostics window.

Stop after handoff for final visual acceptance. Do not connect real Connections data yet.
