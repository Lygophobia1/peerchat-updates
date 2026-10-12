# Compact History Sync authorization — dev.94

## Behavior

The main device row offers a compact blue Authorize pill immediately left of its chevron when verified current metadata requires approval and this profile is an enabled trusted local authorizer. Two unrelated self-grants do not imply pair authorization. A valid one-way custom grant is preserved; History Sync OFF does not erase approval. If a local-issued remote grant already exists but local approval is missing, the row directs the user to approve this PC first. Ineligible issuers are directed to an enabled trusted history device, never automatically trusted or re-enabled.

Authorize opens the existing focused setup dialog. A genuinely new normal grant defaults Receive and Provide ON. Existing custom/local-owned permissions remain unchanged on open; individual switches remain under Advanced. The dialog explains eligible encrypted-history exchange and requires the existing explicit confirmation before native signed-grant issuance. Cancel does not issue grants or persist the edited nickname.

The application refreshes authenticated account/device metadata before issuing. It rechecks local authorizer eligibility and, after durable publication, verifies that the server-current local-issued grant has the exact requested permissions before reporting success. Controlled eligibility failures and bounded native/server failure stages remain visible for retry. Existing native reconciliation alone decides automatic transfer admission; no new authorization protocol or retry owner is introduced.

The bounded local application snapshot has an opt-in approval projection. Legacy callers, including Android, retain the original response. This is not an archive wire-format change. Grant IDs, generations, trust and revocation remain Advanced.

## Validation

- Native enrollment, historical storage, scoped-session, server metadata and mutual-TLS security suites passed: stale generations, disabled/mismatched authorities, revoked/wrong-account devices, durable publication and replay/dedupe remain fail-closed.
- Packaged native application test passed with exact one-way grant permissions, current local ownership, common-issuer pair admission, five worker restarts and actual bounded paired history transfer. Host transfer is not physical acceptance.
- Settings tests passed for new ON/ON defaults, preserved ON/OFF custom permissions, cancellation, failure recovery, live committed-batch updates and stale-callback guards.
- Rendered WPF tests passed for compact/narrow layouts, blue pill placement left of chevron, focusability/tab-stop semantics and ineligible-authorizer next steps. Actual display scaling and physical keyboard/screen-reader acceptance remain pending.
- Full Windows build and package/import audits passed. Android JVM tests and debug/release builds passed; ARM64 archive JNI export audit passed.
- Shared/generated Windows and Android version audit, Development updater manifest/hash/order and safe extraction passed. `git diff --check` passed.
- The Settings test wrapper now retains its process handle so a fast successful process exposes its actual exit code; assertions are unchanged.

Candidate: `build/development/4.0.2-dev.94/DevelopmentUpdater/PeerChat-4.0.2-dev.94.zip`

SHA-256: `34d67d1f2d60a50bf372c2cff3886656702a264502d03073f9fe082582b58762`

Unsigned Development policy only; no publication or merge. Dev.93 is an intermediate build superseded by dev.94's one-way self-grant display and stricter preflight/cancellation behavior. Generated binaries/evidence are excluded from source commits.

## Separate pending transfer investigation

The supplied physical report now has current authorized source/reader grants and 13 processed messages, but stops at `record_device_mapping_missing`. This checkpoint does not fix or bypass that provenance check and does not require reauthorization. Historical sender/recipient account bindings must be proven from authoritative data before any correction; message-supplied account claims are not trusted. Existing identities, history and grants remain untouched. Group text-history and group diagnostic/media milestones remain separate follow-on work, not acceptance claims for this authorization UI.

Physical check: Authorize → review defaults/custom permissions → Cancel, then explicitly Confirm on a genuinely eligible device; verify the row updates, disabled/custom grants remain unchanged, automatic sync starts only with valid admission and ON policy, and actual imports—not approval labels—advance the received count.
