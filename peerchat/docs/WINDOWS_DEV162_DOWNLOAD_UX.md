# Windows dev162 download workflow and test handoff

Download now records the selected filename once, retains the existing internal staging copy, and automatically saves a fully verified copy. The same Windows candidate removes Save As from DM and group bubbles. Saved attachments offer Open File and Open Folder; failed saves and legacy downloads without a saved destination offer Retry Save.

Physical acceptance of this download workflow is pending. Previously accepted group delivery, clipboard screenshots and the optimized DM transfer engine remain the baseline. The live Pi and Android application were not modified or installed.

The existing worktree is `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`, branch `codex/native-unicode-emoji`, based on dev160 checkpoint `088f342`. Logical implementation checkpoints are `719e500` (verified storage/export), `1764972` (durable DM/group automatic save and UI) and `3f02a5f` (published recovery proof and final save states). The ZIP manifest records the final handoff checkpoint and exact hashes.

## Confirmed cause

The old Download dialog collected a filename, but acceptance passed its directory to the native receiver. DM acceptance created an ID-owned directory containing the original basename; group finalization used an ID-prefixed internal basename. Neither path persisted a separate final export choice or automatically invoked the verified export operation. Save As was a second, independent action. This was a download and export lifecycle defect, not corruption of authenticated sender metadata.

## Implementation

- `desktop/GroupAttachments.cs` supplies the shared Save File dialog, original-name defaults, first available `name (1).ext` numbering, exact custom selection, and explicit overwrite confirmation. Native file identity is captured before confirmation. Declining keeps the dialog open; cancelling does not accept the transfer.
- `desktop/ChatAttachments.cs` removes Save As, refreshes when save-only state changes, and exposes saved-file actions or Retry Save. Historical saved files remain accessible even when their internal preview path is unavailable.
- `windows_download_save.hpp` records DM save choices in profile/device-bound DPAPI sidecars. Its bounded hot index archives completed choices by transfer ID rather than imposing a lifetime download limit. Writes occur at durable transitions, not for live progress samples. TransferManager remains authoritative for network completion and ACKs.
- `node.cpp` accepts DM payloads into profile-owned ID staging. Its existing worker services one bounded verification/copy step at a time after completion. Completed source tokens are retained independently of transfer-record eviction. Legacy locally recorded attachment tokens support Retry Save.
- `windows_group_attachment_application.hpp` persists the selected export separately from its verified internal output. New jobs use supplemental `AttachmentJob/v4` records; existing v1–v3 records remain readable. Restarted downloads do not attempt export before plaintext verification. Group receipts still acknowledge verified internal completion, independently of whether a removable destination is available.
- `windows_group_attachment_output.hpp` shares fresh full-file verification and bounded export between DM and group. An export partial has a durable identity token, a second hash, flush and no-replace publication. Restart can recheck an already published inode or recopy a retained partial. Cancellation deletes only a proven owned incomplete copy.

## Overwrite and failure safety

No-consent publication always uses no-replace rename. A file appearing after selection causes a safe failure and Retry Save, not replacement. A newly confirmed replacement is bound to the exact path, volume, file ID, length and last-write time. These checks run again before replacement; confirmation does not authorize a different occupant.

On supported local Windows filesystems, a verified replacement is prepared and flushed before the confirmed old file moves by its own handle to a unique recovery backup. The new copy is then published without replacing any competing name. The backup is deleted only after completion has been durably recorded. This is not a claim of a single atomic swap: there can be a brief name gap between the two renames. No incomplete destination file is published.

Retrying cleanup of an already published replacement requires a read-only proof of the original operation's new inode, absent partial, and matching old approved backup. A fresh full hash still gates backup deletion. A backup's mere existence is not overwrite permission; unrelated or changed occupants remain rejected.

Conflicting occupants, changed identities, reparse points, missing storage and access failures fail closed. The verified internal source is retained. If a conflict prevents safe restoration of an overwrite backup, both copies and protected recovery tokens remain; do not manually delete `.peerchat-replaced` files while investigating. A missing volume may need reconnecting before its owned partial/backup can safely be retired. Retry Save can choose another destination when no unresolved backup prevents cleanup.

Local save errors distinguish unavailable/changed internal content from destination failures. A completed transport alone is never reported as a successful user save. External files subsequently edited or deleted by the user are not treated as authoritative internal history.

## Compatibility boundaries

No server RPC, wire format, E2EE, signed grant, group key envelope, transfer journal schema, chunk size/window or 256 MiB limit changed. Windows-local commands add selected-destination acceptance, overwrite identity capture and retry; old folder-based commands remain for compatibility and tests. Original authenticated basenames stay unchanged in message metadata.

DM save choices are optional isolated sidecars. Group v4 supplemental job records require this newer Windows controller; an older controller is not expected to resume a newly created v4 job. No old records are migrated or deleted. Preserve profiles and use the newer client again rather than clearing storage after a downgrade. Dev160 and earlier accepted packages remain available.

## Automated validation

Passed focused checks include:

- Original/custom Unicode basenames, duplicate isolation, unsafe-name refusal, legacy sink tokens and restart reopening.
- Full hash before access, corrupt/substituted source rejection, reparse refusal, no-consent collision, identity-bound overwrite, crash backup restoration, rival-file preservation and idempotent retirement without deleting completed files.
- DM automatic save, exact remembered target, partial-save restart, missing destination and retry, source recovery after transfer-journal eviction, and completed save history beyond the 64-entry hot index.
- Real Node mutual peer TLS over an opaque relay: interrupted transfer, profile close/reopen, reconnect, final hash, automatic user save and message ACK coexistence.
- Real application/TransferManager/HPKE/managed DiscoveryClient/pinned TLS/server-cache fixture: one upload, two recipients, original-evidence rejection, key ACK recovery, download/finalization restarts, automatic saved copies, approved overwrite, unavailable destination and Retry Save, independent receipts, sender offline, server restart and revocation/late-member restrictions.
- WPF dialog naming, local selection encoding, no Save As actions, saved/failed/pending/legacy controls, save-only refresh, preview bounds and historical-file access. Clipboard screenshot handling and accepted emoji/composer regressions also pass.
- TransferManager integrity, duplicate ACK, disk failure, cancellation, expiry and protected restart regressions.
- Negotiated 32/64 KiB × 1/4/8/16 credit/integrity tests and packaged legacy fallback; concurrent H.264 media stop, local mailbox rejection and durable transfer recovery with fresh relay pairing.

Key isolated evidence:

- Group application: `F:\PeerChat-isolated-cache-tests\application-7bed7196dd5d4e238eebdfc0da480200`.
- Verified output: `F:\PeerChat-isolated-cache-tests\windows-group-output-3feeb72f02ff49628d7cde03fa29488c`.
- Save coordinator: `build/tests/download-save-bf34ade4b809431a996dff589b775aba`.
- Real DM session: `build/tests/auto-save-session-final`.
- Final bubble UI: `build/tests/group-attachment-ui-0835785130b440b18fbc69994958c221`.
- Pipeline: `build/tests/pipeline-49daa2bd9dfe4151a0b03e2d10b0546b`; media coexistence: `build/tests/media-transfer-c421e53cd51543bd9135b2a0d0f74565`. These are isolated host measurements, not Pi/LAN throughput or physical media acceptance.

The Windows all-components build passed. Native and managed candidate components were subsequently rebuilt in the same unpublished dev162 directory for the final save/error and historical UI changes. Existing managed warnings remain; native compilation uses warnings as errors. Package auditing records the final source checkpoint and payload hashes. No synthetic test is physical acceptance of the Save File dialog or Pi delivery.

## Physical test checklist

Extract the new Windows ZIP to a separate folder. Close the previous client and open the new executable using the existing profile; do not reset data or permissions.

1. In both DM and Family group, Download a picture/file with its original name, then another using `cat2.png`. Confirm each appears automatically at the selected exact path only after verification, without a second action or ID prefix/folder.
2. Repeat with the original name already present. Confirm numbered defaults, then explicitly select an existing file: No must allow another selection, Cancel must leave the offer untouched, and Yes must replace only after verification.
3. Restart during download, let it resume, and confirm the remembered path and integrity. Try concurrent downloads with identical names; no existing file may be silently replaced.
4. Make the chosen destination unavailable before saving. Confirm the internal download survives, Retry Save is offered, and saving succeeds once access is restored or another safe destination is selected.
5. Check Open File/Open Folder on saved and historical attachments, and Retry Save on a legacy unsaved download. Confirm no DM/group bubble has Save As.
6. Recheck a clipboard screenshot and one negotiated 64 KiB × 16 DM transfer. Existing data, groups, messaging and encrypted cache operation must remain intact.

Stop for physical acceptance after this package. Do not deploy to the Pi or resume other milestones.
