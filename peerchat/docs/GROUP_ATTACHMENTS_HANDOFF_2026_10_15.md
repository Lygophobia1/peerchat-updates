# PeerChat group attachment development handoff

Development is stopped at the user's request. All saved source changes and completed subagent work are preserved. The server context and receipt component and the Windows durability helpers have focused passing tests. The integrated Windows attachment controller, UI and actual-server harness remain WIP. No complete group attachment candidate or physical acceptance is claimed.

## Repository and checkpoint state

- Git worktree root: `C:/Users/Livel/.codex/worktrees/native-unicode-emoji/Create 2D Game`.
- Project directory: `C:/Users/Livel/.codex/worktrees/native-unicode-emoji/Create 2D Game/peerchat`.
- Branch: `codex/native-unicode-emoji`.
- Starting baseline of this completion pass: `724116d`.
- HEAD found during recovery: `96cf7aa7639f72f8ad81f7d4a5cb688ec9b90079`.
- Frozen implementation HEAD: `3908e76d403791e1c061f229e2a2cfa13561a48c`.
- Final HEAD is the documentation checkpoint containing this report. Obtain its exact hash with `git rev-parse HEAD`; its hash is also supplied in the final handoff response. A commit cannot embed its own hash.
- Recovery found 11 modified tracked files and 21 untracked source/test files. All were preserved in the checkpoints below; no source was edited during recovery.
- Source checkpoint checks: `git diff --check` and staged whitespace checks passed. No new build, regression suite or benchmark was started during recovery.
- Build outputs and test evidence remain on disk outside the source commits. No accepted development package was replaced.
- All development subagents are stopped, completed or errored. The durability agent's final review was interrupted by usage limits; its earlier completed helper results were recovered.
- The old native build session 87196 no longer exists. Recovery found no running g++, cc1plus or collect2 process. Its final build exit status could not be recovered.

| Checkpoint | Status | Contents |
| --- | --- | --- |
| `f444265` | Prior accepted baseline, preserved | dev.148 baseline beneath the accepted emoji development line |
| `29566b3` | Earlier validated component | Bounded protected packet expiration and cleanup |
| `724116d` | Starting checkpoint | Parallel Android interface inventory and integration manifest |
| `96cf7aa` | Validated server component | Frozen cache context and signed recipient verification receipts |
| `18b6082` | Validated helpers | Protected transfer state/history, bounded output verification/export, RPC codec and tests |
| `3908e76` | Explicit WIP | Windows controller, Node/bridge/UI integration and production integration harness |

The report and updated integration manifest are committed in the final documentation checkpoint. The intended final working-tree state is clean, including no untracked source files. Ignored build/evidence directories remain intentionally preserved.

## Completed components and validation boundaries

### Server context and recipient verification receipts

Files: `server/AttachmentCacheAdmission.cs`, `AttachmentCacheRequests.cs`, `AttachmentCacheRuntime.cs`, new `AttachmentCacheContext.cs`, `AttachmentCacheReceipts.cs`; tests `tests/attachment_cache_context_test.cs` and `tests/portable/PeerChat.CacheContextTests.csproj`.

Admission freezes a bounded original roster snapshot with the immutable object. The new authenticated `cache_context` response returns the original roster, current roster, original/current membership incarnations, descriptor and immutable completed-object expiry. Clients must still verify signed ledger evidence; the server response alone never establishes historical entitlement.

`cache_verified` accepts a recipient-signed attestation only after that recipient's durable ciphertext cursor reaches the full size. It binds the exact descriptor, account, device, public identity and verification time. Exact retries are idempotent; conflicting receipts are rejected. `cache_receipts` exposes independent durable progress and actual signed verification evidence to the authenticated sender. Ciphertext completion, key receipt and recipient plaintext verification remain separate facts.

New supplemental cache tables use foreign-key cascade ownership. Existing pre-release objects lacking frozen context fail closed instead of manufacturing historical evidence. The shipping cache gate remains closed.

Previously completed validation covers actual TLS cache requests, independent recipients, sender logout, restart, security and expiry checks; runtime/security/process-crash regressions; Framework and portable server builds; and a shipping-assembly gate test proving an enabled configuration still creates no cache database. These are server/component results, not proof that the new full Windows workflow works.

### Protected transfer state and bounded receiver finalization

Files: `windows_group_attachment_transfer.hpp`, `tests/windows_group_attachment_transfer_test.cpp`, `tests/test-windows-group-attachment-transfer.ps1`.

`CacheStateStore` composes the existing ordinary transfer journal with protected supplemental cache-transfer records. Cache purpose records stay out of the legacy peer-file journal. It retains protected output intent before plaintext writes, uses CAS for supplemental application data, and supports exact source/sink identity recovery.

`ReceiverFinalizer` consumes the completed ciphertext through bounded steps, verifies AEAD blocks and both ciphertext/plaintext hashes, and publishes output only after verification. It recovers partial plaintext, fully written plaintext, and already-renamed output. Its generic immutable-source overload also supports sender preview from original staged ciphertext.

Passing tests include actual process exits at multiple finalization stages, exact Unicode output, mixed legacy/cache isolation, protected state bounds/CAS, exclusive ownership, corruption rejection and sender staged-source finalization.

### Supplemental attachment history

Files: `windows_group_attachment_history.hpp`, `tests/windows_group_attachment_history_test.cpp`, `tests/test-windows-group-attachment-history.ps1`.

`HistoryStore` uses the existing Windows SQLite provider in a separate profile-local `group-attachment-history/peerchat.sqlite`. Private job payloads and their indexed bindings are protected. It changes no message-history or transfer-journal schema.

The store provides immutable bindings, revision/payload CAS, bounded paging, a separate limit of 64 active jobs, and completed history beyond that active limit. Default database budget is 512 MiB; individual protected payload limit is 512 KiB. Unchanged saves avoid a write transaction and re-encryption.

Passing tests cover 205 lifetime records, 64 active jobs, paging/counting, immutable bindings, stale revisions, no-op saves under another write lock, profile-copy rejection, index tampering, disk/database-capacity rollback and foreign-schema isolation.

### Verified local access and exact-name export

Files: `windows_group_attachment_output.hpp`, `tests/windows_group_attachment_output_test.cpp`, `tests/test-windows-group-attachment-output.ps1`.

`VerifiedOutput` reopens the retained file identity, hashes in bounded steps, and exposes a path/read access only after success. A retained read handle and pinned directories prevent mutation or ancestor substitution during access.

`OutputExport` copies and hashes in bounded steps to an owned new temporary file, then atomically publishes the exact selected filename without overwrite. Cancellation removes only its own temporary file. A crashed export is not automatically adopted; the same operation ID fails safely and a fresh operation ID can retry.

Passing tests include zero/boundary/Unicode files, same-identity mutation, substituted files, read-lease enforcement, existing and racing destination collisions, cancellation, actual process interruption, and real NTFS junction rejection.

### Bounded RPC value codec

Files: `windows_group_attachment_rpc.hpp`, `tests/windows_group_attachment_rpc_test.cpp`.

This Windows-local bridge codec carries typed bounded values between native and managed code. It enforces UTF-8, depth/node/collection/string limits, integer bounds, duplicate-key rejection, and canonical base64/base64url. It does not define a replacement peer messaging protocol.

The focused native codec test passed, including truncations, duplicate keys, invalid values and payloads up to 65,536 bytes. The matching managed codec and session-isolation tests also passed.

## Latest Windows application integration preserved as WIP

### Controller and worker ownership

Files: `windows_group_attachment_application.hpp`, `node.cpp`, `node.hpp`, `ui_bridge.cpp`, `windows_mailbox.hpp`.

The new `Application` borrows the existing Storage, profile, TransferManager, authenticated group context and mailbox-owned HPKE/identity providers. Node's existing worker services it. It adds no socket owner, private encryption-key registry or second TransferManager.

Implemented paths include bounded immutable ciphertext preparation, signed descriptor creation, cache RPC translation for existing TransferManager frames, per-device envelope preparation/publication, incoming envelope discovery/decryption, explicit download acceptance, local verification, signed plaintext-verification receipts, independent recipient projections and protected job restart state.

Node wires purpose-scoped transfer routing and excludes cache objects from ordinary DM attachment catalog/actions. The managed discovery client remains the pinned-TLS RPC owner. Attachment failures have a separate handling boundary rather than intentionally terminating normal messaging.

The new controller's last standalone compilation passed with `-Wall -Wextra -Werror`. That is a syntax/compile result, not a completed production integration test. Later source edits and the final Node scheduling/filtering changes still require a fresh full build and regression run after development is authorized again.

### Recovery corrections already saved in the WIP

- A received encrypted envelope with no decrypted body is retained as pending context, instead of being treated as interrupted outgoing preparation.
- Each application job has its own protected history record rather than one lifetime-limited monolithic snapshot.
- Restart forces recipient-key discovery before publication so an empty in-memory prepared packet is not published.
- Sender resume probes `cache_status` and only admits a genuinely new object. An already admitted old-epoch object does not rely on re-admission under a newer roster.
- Sender upload completion is reconciled with the existing TransferManager record.
- A locally completed receiver ciphertext is considered for finalization before requesting another server lease.
- The finalizer output token is saved durably before its temporary journal intent is retired.
- The signed verification receipt is generated once and reused exactly for retry.
- Original/current roster fields and membership evidence are checked, with bounded expiry conversion.
- Actual server expiry/unavailability error names map to an unavailable attachment state.
- Capability support depends on a successful authenticated cache probe instead of merely being signed in.
- Prepared staging removal uses move construction rather than requiring the deliberately deleted Manifest move assignment.
- Local open/save uses asynchronous bounded verification/export rather than exposing a file after only checking its path or length.
- Completed source/recipient receipt states remain distinct from upload completion.

These corrections are implemented, but their combined production restart behavior is not validated. The passing helper tests establish only the helper boundaries described above.

### UI and authenticated managed bridge

Files: `desktop/GroupAttachments.cs`, `GroupAttachmentExecution.cs`, `App.cs`, `ChatAttachments.cs`, `ChatViewModel.cs`, `DiscoveryClient.cs`, `Main.xaml`, and the managed file list in `build.ps1`.

The saved UI includes group file selection, drag/drop, an in-memory bounded clipboard PNG draft/preview, one logical attachment bubble, per-recipient progress, retry/cancel/download controls, and verified open/save handling. Ciphertext confirmed-byte progress has a separate transfer-size denominator from the plaintext file size.

The bridge captures profile/account/session/pinned-server context, limits outstanding work, rejects stale generation responses and checks authenticated context before and after network operations. Pending open/save actions are scoped to the same profile, discovery owner and selected group.

Managed build and UI/bridge tests passed. Those tests cover logical-bubble deduplication, filename and verification gates, expiry preserving completed local access, draft/drop scope, PNG bounds, XAML layout, binary bounds, late-response suppression and authenticated scope rejection. The accepted emoji regression also passed earlier, including color catalog, 100 insertions, favorites/profile persistence, Unicode editing and compact composer.

An existing DM attachment regression fails a font assertion against both baseline dev.154 and the new build: it expects only Segoe UI whereas the existing implementation also specifies Segoe UI Emoji. The assertion was not changed to force a pass. No new DM regression cause has been demonstrated.

### Known unfinished behavior and review points

1. The complete Application-to-actual-server fixture has not passed. No full upload/download/import success is claimed.
2. The latest fixture fails at `Application enqueue: Invalid peer message`. The controller now requires the authenticated capability probe before enqueue, but the fixture still enqueues first. This is the agent's identified harness mismatch; fix and rerun to reveal any subsequent product failures.
3. The fixture also assumes operation 6 returns a path synchronously. It must pump bounded output work until verification returns a path.
4. Async open/save has managed/helper tests, but the combined native/UI workflow remains unexercised.
5. The last immediate-continuation scheduling change and direct-purpose demand filtering need full rebuild/regression.
6. Selected-group recent history is projected; UI controls for older history paging are not implemented.
7. Historical roster reconstruction intentionally blocks prefixes involving unsupported device-enrollment receipt replay. Do not enable enrollment flags or fabricate proof merely to pass those cases.
8. Interrupted preparation before immutable material is retained is surfaced as a failed preparation requiring original-file selection. Validate cleanup and retry UX; do not reuse partially used key/nonce material with changed plaintext.
9. Cache lease interruption, overlapping RPC ordering, cancellation, retry, unavailable keys, terminal verification failures, and receipt persistence still need combined production tests.
10. Full active-job/completed-history pruning and bounded packet/staging cleanup need integrated lifecycle validation.
11. Default download pull scheduling currently uses bounded 32 KiB reads. Do not assume upload chunk/window tuning establishes equivalent download pipeline behavior or bandwidth-cap enforcement.
12. Missing verified historical evidence remains an explicit block. No old group-key distribution or identity migration is authorized.
13. Safe error messages and feature-unavailable presentation need physical review once the workflow passes automated validation.

## Preserved validation evidence

These directories were retained; the recovery pass checked their existence and read available result logs without rerunning tests.

| Evidence directory | Result and boundary |
| --- | --- |
| `F:/PeerChat-context-final-93bba37e1eeb479eb2e6519b8caef471` | Server context/receipts TLS, restart, independent recipient and security checks reported PASS |
| `F:/PeerChat-context-regression-a7e012c4ae804c5089726de3c8169e35` | Cache runtime/security/process-crash regressions reported PASS |
| `F:/PeerChat-cache-server-build-efec3b29b616443d9e524044c20cefc9` | Framework and portable server builds reported PASS |
| `F:/PeerChat-context-shippinggate-b90f913abcc24157a6c6cc45c8b7aacf` | Shipping assembly cache gate closed over actual TLS; no cache database |
| `F:/PeerChat-isolated-cache-tests/windows-group-history-db175a72c1674374984e81e5cdf55021` | History helper PASS; result log verified during recovery |
| `F:/PeerChat-isolated-cache-tests/windows-group-transfer-aa9497b6106d46aa957f1e1bb37f4e88` | Fresh-process finalization and source-preview helper PASS; result log verified |
| `F:/PeerChat-isolated-cache-tests/windows-group-output-a9bad4e1eb8e4abdba9e8d3502716e38` | Bounded access/export, tampering, collisions and junction tests PASS; result log verified |
| `F:/PeerChat-isolated-cache-tests/windows-group-rpc-test.exe` | Focused native codec test reported PASS |
| `build/tests/group-attachment-ui-7363eb9830be4f08895196837d28e481` | Latest managed UI/bridge suite reported PASS |
| `F:/PeerChat-isolated-cache-tests/application-a935b86905694e02aa8cfa6ed5428146` | Full Application harness failed at enqueue; not an end-to-end PASS |

The failed integration fixture is retained under `fixtures-316b9df3b55e4a85ab8eb176ac75260a` within its evidence directory. Do not copy its isolated credentials or key material into reports or production profiles.

## Build and package status

No new versioned Windows group-attachment test package or Raspberry Pi candidate package was completed. No cache feature gate was opened. No new physical acceptance is claimed.

Preserved intermediate output directory:
`C:/Users/Livel/.codex/worktrees/native-unicode-emoji/Create 2D Game/peerchat/build/tests/group-attachment-ui`.

Recovery observed:

| File | Size | Last write UTC | SHA-256 |
| --- | --- | --- | --- |
| `PeerChat.Core.dll` | 11,265,108 bytes | 2026-10-15 05:41:43 | `1CC526C7AC742B07C3753F97EF9BA8B54311B1F15FAC080F80FA52F02DF3FC3F` |
| `PeerChat.Desktop.exe` | 7,586,816 bytes | 2026-10-15 05:39:24 | `9B05EFCEC09C58A1BE4218EF1109DB34B3006788B44F05C6E661C857E3D1752A` |

The DLL exists, but the corrected native build's final exit status was lost with its session. Its source coverage may predate the final edits. These are intermediate artifacts, not a package audited against the frozen WIP checkpoint. Keep prior accepted dev.148/dev.153 and existing dev.154 outputs available; no replacement or cleanup was performed.

No current production-path throughput comparison exists. Earlier component/host results must not be presented as physical Pi/LAN performance. No new benchmark was started during the stop/recovery work.

## Shared interfaces and Android dependencies

The separately owned Android worktree was not edited or merged. The last inspected Android branch was `codex/android-groups-voice` at `d67b0e5`, with subsequent WIP interfaces described in its `docs/ANDROID_GROUPS_VOICE_INTEGRATION.md`.

Its planned/active owners include `portable/peerchat_core.cpp`, `portable/reliable_endpoint.hpp`, `portable/relay_requests.hpp`, `session_transport_manager.hpp`, `group_text.hpp`, new `portable/group_application.hpp`, and Android group bridges/execution/UI. Those shared files were not modified by this completion pass.

This pass changes Windows-private `node.cpp`, `node.hpp`, `ui_bridge.cpp` and `windows_mailbox.hpp`; managed Windows files; additive private headers; server cache files; and tests/docs. Existing encryption providers, group encryption format, PCGE admission, history schema and portable JNI/C ABI remain unchanged here.

New Windows C exports:

- `ui_group_attachment(handle, operation, group, id, path)`: operation 0 snapshot, 1 selected file, 2 accept destination directory, 3 cancel, 4 retry, 5 asynchronous export to exact destination, 6 asynchronous verified local path, 7 in-memory clipboard PNG.
- `ui_group_attachment_network(handle, operation, input, length, output, capacity)`: operation 0 authenticated session binding, 1 next request, 2 typed response, 3 safe network failure. The local buffer limit is 100,000 bytes.
- Local domains are `PeerChat/AttachmentSession/v1`, `PeerChat/AttachmentResponse/v1` and `PeerChat/AttachmentFailure/v1`. Responses carry request identity and authenticated generation.
- Empty open/export results mean bounded verification/export is pending. A returned path requires verified local output; `ok` means the selected export completed.

New server operations are `cache_context`, `cache_verified` and `cache_receipts`; existing cache operations remain in use. Completed ciphertext expiry is seconds UTC on the server and checked milliseconds UTC in key envelopes. Binary cache fields use canonical unpadded base64url except the existing descriptor admission and roster creation-proof fields, which use standard base64.

Messages remain bound to the existing signed descriptor and attachment-specific HPKE wire. Per-device envelopes are not inserted as different PCGE messages with one shared message ID. Supplemental protected attachment history owns the logical attachment identity and recipient evidence.

Android does not implement this new attachment execution/UI contract here. Future integration must inspect its then-current branch, agree capability negotiation and group application ownership, and preserve unsupported-feature behavior. No automatic trust, new device registration, old epoch key release, or shared protocol rewrite is authorized by these WIP changes.

## Remaining end-to-end validation

After development is explicitly resumed, the minimum completion gate remains an isolated actual server, one source and two independently authenticated recipients using production client paths.

Required coverage still includes upload once; sender offline before download; both independent plaintext/hash completions; recipient/client/server interruption and restart at preparation/upload/key/accept/download/finalization/receipt stages; exact durable resume; duplicate delivery; revocation; later-added/rejoined/replaced devices; missing historical evidence; eligible old-epoch recipients with no full historical group key; wrong-device HPKE rejection; expired/evicted objects; concurrent DM text/files, group text, History Sync and media.

Run the negotiated chunk/window matrix and bandwidth/media-contention checks only after correctness passes. Preserve 32 KiB by 1 as default and the 256 MiB ordinary attachment limit. Distinguish host fixtures from physical network measurements.

## Exact resume sequence

1. Obtain explicit authorization to resume development. Stay in this worktree and branch; do not reset or merge.
2. Read this report and `docs/TRANSFER_CACHE_INTEGRATION_MANIFEST.md`. Run `git status --short` and `git log -4 --oneline`; compare with the final handoff hashes. Inspect any newer user changes before editing.
3. Re-read the Android branch's current integration manifest before touching a shared interface. Do not edit that worktree or duplicate its application/voice work.
4. Fix only the known integration harness sequencing first: service the authenticated `cache_pending_keys` capability handshake before enqueue; update operation 6 to pump until a verified path is available. Preserve the failing fixture and its evidence.
5. Rebuild the current native/managed sources, then run `tests/test-windows-group-attachment-application.ps1` with isolated storage. Trace the first actual failure through controller/RPC/TransferManager state. Do not weaken assertions or security gates.
6. Revalidate affected helper boundaries using the existing `test-windows-group-attachment-transfer.ps1`, `test-windows-group-attachment-history.ps1` and `test-windows-group-attachment-output.ps1` scripts only where changes warrant it. Use `test-group-attachment-ui.ps1 -ApplicationDirectory <rebuilt-directory>` for UI/bridge regressions.
7. Complete the missing lifecycle cases and older-history UI before calling the feature usable. Review retry/cancel semantics, capability failures, protected metadata budgets and exact receipt retries.
8. Run relevant ordinary messaging, DM attachment, group, History Sync, emoji and media regressions. Resolve or explicitly qualify the existing baseline font assertion without concealing a real regression.
9. Only after the whole actual-server path passes, consider a separately gated test candidate, versioned Windows/Pi builds, package audit, measured pipeline comparison and deployment/rollback instructions. The cache remains disabled until its required gates pass.
10. Stop for physical acceptance and live deployment approval. Do not deploy, install on Android, alter existing identities/history/grants, or merge branches as part of resuming this checkpoint.

## Stop and preservation confirmation

No live Raspberry Pi inspection, restart, database mutation or deployment occurred during this completion/recovery pass. No Android worktree edit, phone installation or device-data cleanup occurred. No branch merge, reset, clean, force push or history rewrite occurred.

All 32 recovered source/test/doc changes are committed across the helper, WIP and documentation checkpoints. Source contents were preserved; only this handoff report and a manifest status addendum were written during recovery. Development remains stopped.

