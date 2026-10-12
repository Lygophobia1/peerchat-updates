# History Sync compact Windows UI — dev.90

## Scope and presentation correction

Windows Privacy & Security now uses a left-aligned, maximum 720 logical-pixel column. Offline Messages and History Sync retain separate existing preference commands. The status card has 12-pixel corners and 18-pixel padding; device headers are 68 pixels high. Narrow windows shrink to the scroll viewport and scroll vertically. The sidebar is unchanged.

The terminal-state projection previously depended exclusively on the transient `Complete` flag. A restored native `History synchronized` snapshot without that flag could lose the completed presentation. The projection now recognizes that native terminal state only when the operation is not running. A new running Connecting operation cannot inherit completion from an older flag; its progress is indeterminate and reset to zero. Last-success information remains separate.

Counts are newly imported, committed messages, not scanned records. Failed operations retain the verified imported count. A completed operation with zero imports says “No new messages — up to date.” There is no reliable total in the current native snapshot: active transfer remains indeterminate. A filled terminal bar represents operation completion, not an invented message total.

Missing/unavailable verification metadata is shown as waiting for verification, not asserted to be missing authorization. Approval prompts require authenticated device state indicating approval is needed. Connected is displayed only for the actual active receiving source; otherwise availability remains unknown. Nicknames never replace identity.

The existing history presentation events update Settings without a voice timer. Existing conversation revision refresh and callback account/generation/epoch guards are retained. Closing Settings unsubscribes its view, not the native synchronization worker. Manage/Advanced retains custom permissions, explicit trust, grants and revocation without rewriting them.

No archive protocol, native storage, TLS, networking, identity, media, or group behavior was changed.

## Validation

Passed against the packaged dev.90 client:

- `tests/test-history-visual.ps1`: rendered 1920×1080 and 920×780 logical windows; compact/shrinking width, terminal restart projection, Connecting with a stale completion flag, live committed batches, retained failure count, zero-import completion and inline device expansion.
- `tests/test-archive-settings-ui.ps1`: independent protected mailbox preference, corruption fail-closed, slider states, explicit consent/permission actions, protected nicknames, stale callbacks and event lifetime checks. Existing test compiler emits CS4014 warnings; test passes.
- `tests/test-archive-application-boundary.ps1`: actual paired native workers/scoped relay/mutual Schannel TLS, verified history and attachment transfer, five worker restarts, registration/consent/account/logout/publication boundaries.
- `tests/test-archive-security.ps1 -Enrollment -HistoryStorage -SessionScope -ServerMetadata -PeerTls`: verified imports, restart/replay/deduplication/cursors, cancellation/revocation, wrong scopes/accounts/devices, tamper and signed server metadata.
- Full `build.ps1 -All`, package/import audits, dev.89 → dev.90 version ordering, Development manifest/SHA-256 and safe updater extraction/exact runtime hashes.
- Android JVM tests and debug/release builds were rerun only to align existing shared-version audit artifacts; Android UI was not modified or installed.
- `git diff --check`.

Reproduce visual checks from the repository root:

```powershell
.\tests\test-history-visual.ps1 -ClientDirectory build/development/4.0.2-dev.90/Client -EvidenceDirectory build/development/4.0.2-dev.90/ValidationEvidence/after
```

Before/after PNGs are preserved under `build/development/4.0.2-dev.90/ValidationEvidence/{before,after}`. They render the production WPF UI using isolated test profiles and sample verified-state fixtures; they are not physical transfer evidence. Generated evidence/packages are not committed.

Updater ZIP: `build/development/4.0.2-dev.90/DevelopmentUpdater/PeerChat-4.0.2-dev.90.zip`

SHA-256: `2edfd70a440c3c3dbd142522e528e509f21174c3ba34989199f9eeec3d923e76`

Development unsigned policy only. No release signature, publication or merge is claimed.

## Physical acceptance pending

1. Open Privacy & Security on PC and VMTEST; confirm compact column, readable device rows, keyboard focus and normal/narrow-window scrolling at the actual display scaling.
2. Keep Settings and a conversation open during verified imports: counts and conversation contents should update without restarting or a voice call.
3. Connecting must be indeterminate; completion must show up to date and a real last-success time. A no-new-record sync must report no new messages.
4. Close/reopen Settings and restart the client: terminal status and history persist; repeat sync introduces no duplicates.
5. Expand a device and Manage/Advanced: existing custom permissions and disabled authorizers remain unchanged; unauthorised devices cannot sync.

Reliable total progress and broad device presence are not exposed by the native snapshot, so neither is fabricated. Exact physical fonts/DPI, keyboard/screen-reader behavior and new UI physical acceptance remain to be checked. The earlier 22-message VMTEST transfer is prior physical evidence, not acceptance of this new package.
