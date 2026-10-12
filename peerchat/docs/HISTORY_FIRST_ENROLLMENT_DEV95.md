# History Sync first enrollment — dev.95

## Correction

Dev.94 exposed Authorize only after an enabled local authorizer existed. A never-enrolled account had no normal first-device bootstrap action, so the required row pill could not appear. Dev.95 exposes authenticated first-use eligibility from native enrollment state and adds both the prominent Set up History Sync action and the local Set up pill. Eligible other devices show a blue Authorize pill immediately left of their chevron.

Explicit setup composes existing scoped authorizer-consent and signed self-grant operations. Standard setup and new authorization use Receive/Provide ON; existing custom permissions and automatic-sync preferences remain unchanged. Existing grants, retained/disabled authorizers, revocation and pending publications prevent accidental first-use bootstrap. Publication failure does not claim registered approval; the existing durable publication path can retry the exact signed proof.

On the receiving device, an existing verified grant from an eligible registered issuer can expose a normal Approve action. Explicit confirmation trusts that exact issuer locally; it does not issue another grant or automatically trust an unknown device. Automatic synchronization remains subject to native admission, current grants and existing preferences.

The optional local application snapshot has a v2 approval extension. Legacy and v1 callers remain supported. No archive wire protocol, identity, history, cursor or transport ownership change was made.

## Automated evidence

- First-enrollment WPF integration: visible Set up and Authorize pills, blue accent, left-of-chevron placement, keyboard focus/tab eligibility, actual handlers and confirmation/cancellation; real native CNG profile signatures and server-current proof verification; interrupted publication retry, actual ON/ON grants, Approved projection, explicit recipient confirmation and eligible automatic-sync start. The authenticated RPC transport is an isolated fixture, not a production TLS bypass or physical acceptance test.
- Packaged Settings and compact visual regressions: preserved custom permissions, cancellation, last-permission constraint, stale callbacks, live progress and restart.
- Native enrollment, history storage, scoped sessions, server metadata and mutual peer TLS tests: signatures, account/mapping constraints, revocation, stale generations, verified imports, cursors, restart and deduplication.
- Packaged application boundary: paired native TLS history transfer and worker/relogin regressions. Authorization UI success alone is not evidence of record transfer.
- Full Windows build and package/import audit; dev.94 to dev.95 version/updater extraction and runtime hash checks.
- Android JVM tests, debug/release builds and packaged ARM64/JNI boundary audit. No replacement install or device data changes performed in this checkpoint.
- git diff --check.

The first-use offscreen render confirms control geometry, not final theme acceptance: its isolated host does not load the full Settings theme. Packaged compact visual tests separately cover themed rendering.

## Candidate

Version: 4.0.2-dev.95. Development only; not published.

Package: build/development/4.0.2-dev.95/DevelopmentUpdater/PeerChat-4.0.2-dev.95.zip

SHA-256: d9812c1a872e39cb965b1aaaa95066f687a03412a70cd58ca23c0a87d3a25e3d

Physical retest: on a genuinely never-enrolled account, Set up the first device, explicitly Authorize the second, explicitly approve the registered issuer on the recipient if required, and verify automatic admission and actual transfer. Also verify Settings updates while open. Do not erase existing data to create this scenario.

The separate historical record_device_mapping_missing rejection remains unresolved; no sender/account provenance checks were bypassed. Group-history prerequisites and physical acceptance remain separate milestones.
