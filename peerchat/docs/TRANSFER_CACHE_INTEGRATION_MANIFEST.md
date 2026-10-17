# Transfer cache integration manifest

## Networking priority candidate dev 175

Based on preserved dev.174 and audit checkpoint `8cd6287`, dev.175 changes local
scheduling, not transfer or messaging formats. See `NETWORKING_LATENCY_DEV175.md`
for measured boundaries, remaining contention and physical acceptance.

- `eafd49f`: shared `transfer_manager.hpp` prioritizes pending durable controls
  before unrelated source reads; journal/save, pacing and rotated fairness remain.
- `77f137d`: new internal `reliable_bulk_budget.hpp`, Windows `node.cpp` and
  Android/shared `portable/reliable_endpoint.hpp` bound bulk output admission at
  128 KiB without changing TLS record order, v1/v2 negotiation or chunk windows.
- `022cc29`: server `EncryptedMailboxChannel.cs` and shutdown integration in
  `PeerChat.Server.cs` move optional mailbox hints after durable RPCs onto one
  bounded existing worker. No authorization, mailbox wire or ACK change.
- `ffd6931`: Android `MessagingBackend.kt`, `MailboxExecution.kt` and internal
  `ScopedBackendWork.kt` release mailbox/periodic archive transport waits from
  the profile gate; native operations and results retain exact scope checks.
- `32830b8`: version 4.0.2-dev.175. Previous NoDelay/timing/cache-lock-scope
  audit checkpoints remain in its ancestry.

No exported native/JNI signature, group encryption record, authentication
protocol, database schema or cache authorization changes. Rebuild both clients
for the shared header changes. Group cache and direct compatibility semantics
are preserved. No live Pi deployment is included; server benefits are pending
separate approval and installation.

The other project's clean `ac7ab34` and its
`docs/ANDROID_GROUPS_VOICE_INTEGRATION.md` were inspected read-only. Planned
optional endpoint group/voice admissions do not conflict; retain the new bulk
admission predicate when reconciling future endpoint edits. No other worktree
was changed.

## Online transfer compatibility repair

The dev.174 candidate keeps encrypted cache preferred but restores an explicit
online-only compatibility choice when a linked device lacks cache capability.
Windows checkpoints `51df50c`, `5d0efd6` and `cf89d7a` add the strict local selector,
UI preflight and stale-conversation protection. The matching Android integration
uses the same explicit device-only choice and stale-scope rejection. See
`DEV174_TRANSFER_COMPATIBILITY.md` for the final candidate evidence and handoff.

Shared interface changes are local only: Windows file operation 21 accepts
`online:<canonical account>`; Android's existing TransferBridge request accepts a
scoped probe/commit operation 16. Android additionally uses the read-only internal
`ReliableEndpoint::permitsDirectFileTransfer(authenticationGeneration)` predicate.
These require matching Desktop/Core and Kotlin/JNI packages. No C/JNI signature,
peer/server wire format, encrypted record, journal schema or group API changes.
The current parallel Android worktree at `ac7ab34` was inspected read-only before
these changes; its planned enrollment/group/voice interfaces do not conflict.

Direct choice requires the exact verified current recipient and an authenticated
file-capable non-archive winning route; pending choices cannot establish trust or
grant account-wide delivery. Cache missing-capability, HPKE, revocation and frozen
audience checks remain unchanged. Authorization/stale-scope/network failures do
not trigger silent fallback. Existing queued cache jobs are not converted.
The Pi is not changed by this repair; dev.173 remains the compatible server.

## Offline DM cache integration — dev.173 test handoff

As of 2026-10-16, the active branch remains `codex/native-unicode-emoji` in
`C:/Users/Livel/.codex/worktrees/native-unicode-emoji/Create 2D Game`.
The packaged baseline is dev.172 (`e242b50`). Checkpoint `8607fbe` adds independently
saving Android messages and immediate composer clearing; 50 focused UI/presenter
tests passed. Production source is checkpointed at `bdd28e5`, including reviewed
post-admission engine retirement at `82abe0a`. Unified Windows/Android dev.173
packages passed their audits. Pi dev.166 was safely updated to dev.173 after the
automated gates passed; physical phone/cache acceptance remains pending. See
`OFFLINE_DM_CACHE_DEV173.md` for exact artifacts, evidence and rollback location.

The user approved the separately typed offline DM cache proposal and, subsequently,
the new private recovery records for selected source, cache lease and individual
jobs with an atomic index. This does not authorize changing existing message or
transfer-journal schemas. Existing group attachments, clipboard sending and the
256 MiB plaintext limit remain preserved requirements.

### Shared files and compatibility

| Surface | Current change and boundary |
| --- | --- |
| `group_attachment_blob.hpp`, `group_attachment_metadata.hpp` | Add explicit direct context and DM-specific blob AAD; default group bytes remain unchanged. Legacy group metadata rejects direct context. |
| `dm_attachment_descriptor.hpp`, `dm_attachment_body.hpp`, `dm_attachment_receipt.hpp` | New signed public DM descriptor, frozen signed recipient audience, separately typed private HPKE body and independent verified-file receipt. No private filename, key or plaintext digest in public server metadata. |
| `dm_attachment_application.hpp`, `dm_attachment_finalizer.hpp` | Shared cache orchestration and bounded final integrity/decryption steps. Existing TransferManager remains the transfer, retry and durable-offset owner. Recovery integration remains under test. |
| `transfer_manager.hpp`, `portable/dm_attachment_transfer_storage.hpp` | Explicit local `DirectCacheCiphertext=2` purpose; old peer/group values unchanged. Isolated protected journal wrapper delegates ordinary transfers to their existing journal. Add exact-purpose local terminal retirement preserving protected files; no C/JNI, journal or peer wire-format change. |
| `storage.hpp`, `storage.cpp` | Protected supplemental state, per-job CAS, immutable bounded history pages and read-only local context. Existing message/history/journal schemas unchanged. |
| `windows_dm_attachment_adapter.hpp`, `node.cpp`, `windows_transfer_io.hpp` | Windows adapter, scoped staging and additive local file-transfer operations 20–28 (28 is local retained history). No key export or general-purpose signing API. |
| `portable/peerchat_core.cpp`, `portable/attachment_file_io.hpp`, `portable/attachment_file_scope.hpp` | Android production adapter and purpose-aware staging. Add local operations -10 through -15 (15 is local retained history) through the existing transfer bridge; C/JNI method signatures unchanged. JNI/Kotlin must be packaged together. |
| `desktop/DmAttachmentExecution.cs`, `DiscoveryClient.cs`, `ChatAttachments.cs`, `CoreAdapter.cs`, `App.cs` | Bounded command execution on existing authenticated pinned-TLS RPCs; account-context cache selection must not require a live peer route or bypass native verification. |
| Android `DmAttachmentExecution.kt`, `MessagingBackend.kt`, `ServerAccounts.kt`, `AttachmentSnapshots.kt`, `AttachmentCard.kt` | Equivalent Android RPC/session adapter and truthful logical transfer projection. No Android voice or group attachment UI expansion. |
| `server/AttachmentCacheDirect.cs`, `AttachmentCacheDescriptor.cs`, `AttachmentCacheReceipts.cs`, `AttachmentCacheAdmission.cs`, `AttachmentCacheRequests.cs`, `AttachmentCacheServer.cs` | Add typed DM scope using the existing cache, mailbox, verified keys and device-bound sessions. Group admission remains separate. |
| `windows_group_attachment_application.hpp`, `desktop/GroupAttachmentExecution.cs` | Existing cache receiver uses a freshly negotiated local 64 KiB v2 Offer with window 1; cache RPC schema and serialized durable progress remain unchanged. |

The public descriptor domain is `PeerChat/dm-cache-upload/v1`; the audience domain
is `PeerChat/dm-cache-audience/v1`. Device capability registration signs only
`PeerChat/dm-cache-device/v1` with the existing identity provider. Private notices
use existing per-device HPKE envelopes with a distinct attachment body. Receipt
domain `PeerChat/dm-attachment-verified-file/v1` cannot acknowledge ordinary text
delivery or authorize another device. Normal text, PCGE group records, discovery
authentication, opaque relay and peer TransferManager v1/v2 formats are unchanged.

New `cache_dm_*` operations require a compatible candidate server and explicit
signed capability registration for every frozen recipient key. Text-only clients
are rejected during preflight rather than receiving notices they cannot process.
No automatic trust, enrollment or recipient substitution is introduced. Normal
server builds keep the DM transport gate closed; candidate build switches are
`DirectCacheCandidate` and `DirectAttachmentCacheCandidate` respectively.

The parallel Android worktree was rechecked read-only at `ac7ab34`, clean, including
its `ANDROID_GROUPS_VOICE_INTEGRATION.md`. Its earlier mailbox/group interfaces are
already present in this unified development baseline. This pass does not modify
that worktree, create competing group/voice APIs or copy its private configuration.

### Current validation and remaining gates

The authenticated Windows Application/TLS fixture reproduced `cache_upload_already_owned`
after a lost append reply and client-only restart. The client omitted its lease
from protected recovery state; server duplicate ownership rejection is correct.
The approved correction now durably preserves and reuses that exact lease. Real
application/TLS tests pass lost-append and lost-completion responses followed by
client restart, without evicting another owner or suppressing duplicate rejection.

The source token, selected basename/size, staging token and lease are protected
per job before dependent work proceeds. Existing verified finals are replayed by
comparison, never overwritten or deleted to recover a partial. History is paged
through immutable authenticated storage records rather than silently dropping
older jobs at an in-memory limit. Local history reads do not authorize network
downloads. New cache corruption disables only this supplemental feature, retaining
the corrupt evidence and ordinary messaging/journals.

The security audit also corrected DM leases reading beyond the 48-hour deadline:
reads/progress are rejected after expiry, while lease release still succeeds.
Successful durable DM notice publication now reuses the existing mailbox-ready
event/push wake path outside the cache storage gate; this remains a hint, not ACK.

Completed isolated checks include signed DM admission, immutable offline audience,
retired-key access versus new capability registration, revocation of source and
recipient, cross-account rejection, expiry, independent offsets and verified
receipts. Evidence: `build/dm-cache-admission-final.log`. The existing cache quota,
reserved space, group authorization and actual-process crash suite passed in
`build/dm-cache-runtime-regressions.log`. Actual protected storage CAS/journal
restart and native HPKE/controller tests passed at
`build/tests/dm-attachment-body-84061050f5954c4db4342c416fc47f9d`.

Windows actual-candidate application evidence, including two independent recipient
devices, offline sender, server restart, interrupted finalization, exact verified
file replay and both-direction signed receipt aggregation:
`F:/PeerChat-isolated-cache-tests/dm-application-7d69dbfff8824c1fb270ed241a962be9/results.log`.
The complete Android JVM suite passed 309 tests and lint reported no errors;
the final rebuilt production JNI/TLS interoperability gate passed. Retained
logical cache rows explicitly show unavailable throughput rather than inventing
plaintext-speed measurements from ciphertext engine counters.

Those gates have now passed: client source/staging/lease recovery, real Windows/JNI
bidirectional transfers, two recipients, offline sender, ordinary mailbox dispatch
during upload, legacy direct/group regressions, full Windows build/dependency audit,
309 Android tests/lint/APK signature audit, ARM32 build and actual Pi version/loader,
backup and health checks. Final JNI evidence is
`build/tests/android-jni-dm-final-173/results-b67335c215d9451290e02c1e9dfb9040.log`.
The new purpose-only retirement test also covers eight admitted expirations across
restart, failed-save rollback, no Cancel, protected partial retention and a new
usable transfer. Host fixtures do not establish physical phone acceptance or
Pi/LAN throughput. See `TRANSFER_PROTOCOL_AUDIT_DM_CACHE.md` for the boundaries.

The read-only live inspection found Pi dev.166, ARMv7l, existing encrypted cache
enabled at its configured directory with 10 GiB quota and 48-hour retention. New
The approved update preserved that configuration, certificate, database and cache.
Rollback is retained at
`/opt/peerchat-test/rollback/pre-4.0.2-dev.173-20261017T002445Z`.
No older database was restored. No APK was installed and no physical acceptance
is claimed. Future larger media/diagnostics/roadmap work is not started here.

## Archived group cache milestones

This branch owns the Windows transfer and encrypted attachment-cache milestone.
Attachment-only per-device HPKE envelopes are user-approved. Shipping cache
transport remains disabled in ordinary builds. The explicit dev.158 cache
candidate has passed actual-server Windows production-path integration; physical
Windows/Pi acceptance remains pending.

## Current Windows completion — supersedes archived freeze below

Windows dev162 adds local download destination persistence and automatic verified
export for DM and group attachments, and removes redundant Save As bubble actions
in the same candidate. Details: [Windows dev162 download workflow](WINDOWS_DEV162_DOWNLOAD_UX.md).
Shared Windows files: `node.cpp`, `windows_transfer_io.hpp`,
`windows_group_attachment_transfer.hpp`, `windows_group_attachment_output.hpp`,
`windows_group_attachment_application.hpp`, new `windows_download_save.hpp`,
`desktop/CoreAdapter.cs`, `desktop/ChatAttachments.cs`, `desktop/GroupAttachments.cs`.
Windows-only local operation additions and supplemental AttachmentJob/v4 do not
change portable/JNI APIs, network protocols, HPKE envelopes, transfer journals or
Android compatibility. Existing v1–v3 jobs remain readable; new v4 jobs need the
new controller on downgrade/recovery. Verified export primitives are checkpointed
at `719e500`; the final package manifest identifies the complete UI/controller
checkpoint. No deployment or Android-worktree mutation. Dev160 group/clipboard
physical acceptance is preserved; the new download UX awaits physical testing.

The Windows dev.160 repair adds a scoped, Windows-local `ui_mailbox` operation 9
for the existing server enrollment/resume challenges, a DPAPI-protected cache
credential sidecar, explicit asynchronous cache verification, exact DM receive
basenames and composer PNG drafts. No server wire protocol, group encryption
format, transfer limit/window or Android code changes. Details and compatibility
boundaries: [Windows dev.160 attachment repair](WINDOWS_DEV160_ATTACHMENT_REPAIR.md).
The live Pi remains dev.158; no redeployment is part of this repair.

See [GROUP_ATTACHMENTS_DEV158_VALIDATION.md](GROUP_ATTACHMENTS_DEV158_VALIDATION.md)
and [GROUP_ATTACHMENTS_DEV158_DEPLOYMENT.md](GROUP_ATTACHMENTS_DEV158_DEPLOYMENT.md).

Checkpoints: `8c24b4b` (UI), `02fda44` (controller/recovery), `cfc3fa2`
(candidate gate/version). The final package manifest records exact source HEAD,
source hashes and payload hashes. No merge, live deployment or Android change.

Changed shared integration surfaces are deliberately narrow:

- `windows_group_attachment_application.hpp`: controller-only recovery, bounded
  cleanup/paging and protected supplemental `AttachmentJob/v3`; reads v1/v2.
  v2 adds owned lease; v3 adds original input size. No legacy journal migration.
- `node.cpp`: Windows-local operation 8 for older history (strict cursor, <=32
  results). Existing Windows group-attachment exports and response domains remain.
- `desktop/GroupAttachments.cs`, `Main.xaml`: original-file draft/explicit Send,
  per-group history cursor, serialized verified actions and Prepare again.
  Failed unsealed preparation op4 accepts the original path only before immutable
  material/admission; sealed retry retains exact ciphertext and key packet.
- `server/AttachmentCacheServer.cs`, portable project and build scripts: explicit
  `PEERCHAT_CACHE_CANDIDATE`, not `PEERCHAT_CACHE_TESTS`. Disabled configuration
  remains the default; standard compilation remains gated. No RPC/wire change.
- `server/PeerChat.Server.exe.config`: Framework modern path handling for pinned
  volume-GUID storage; installed beside the server and Admin's server copy.
- Tests add real server-process invocation and repeated restart, missing-evidence,
  cancellation, two-recipient receipts and filename/export verification.

Server operations, immutable descriptors, HPKE envelope formats and original/
current membership checks remain the previously reviewed contracts. Native
portable/JNI APIs, Storage group-text schemas, authentication, PCGE and media
protocols are unchanged. Future Android integration must consume the same
attachment descriptor identity and independent receipt authority, not store
different recipient envelopes as identical group-message wire.

Reinspected the separate Android worktree read-only at `ade45a4` (code `227ce31`),
including `ANDROID_GROUPS_VOICE_INTEGRATION.md` and handoff. It owns portable
`group_application`, scoped session/relay, JNI/Kotlin group text/voice. It has no
cached group-attachment UI. These Windows-local additions do not implement or
replace those interfaces. Its own WIP dev.157 APK is not this Windows/cache
candidate; Windows/Pi final version is dev.158. Do not auto-merge/cherry-pick.

The final Framework dev.158 and separate portable candidate both pass actual
sender/two-recipient TLS/HPKE/cache tests, sender offline, restarts, signed
plaintext receipts, late/rejoined denial and missing authenticated original
evidence rejection. Default-gate and no-test-hook metadata audits pass. Linux
ARM32/ARM64/x64 build audits pass; Linux execution and physical group delivery
are pending. This milestone stops for physical testing and deployment approval.

## Archived development freeze and recovered handoff

Development stopped at the user's request on 2026-10-15. The complete recovery,
file inventory, validation evidence, package status, known failures and resume
sequence are in [GROUP_ATTACHMENTS_HANDOFF_2026_10_15.md](GROUP_ATTACHMENTS_HANDOFF_2026_10_15.md).

The completion matrix and planned interfaces below describe the earlier plan.
The following frozen state supersedes their pending/implemented status:

- `96cf7aa`: validated server context and signed recipient verification receipts.
- `18b6082`: focused-test-validated protected transfer/history, RPC codec and
  bounded verified access/export helpers.
- `3908e76`: explicitly unfinished Windows Application, Node, managed UI/RPC
  integration and actual-server harness. Full production integration has not
  passed; the cache shipping gate remains closed.

The additive Windows exports `ui_group_attachment` and
`ui_group_attachment_network` are implemented in that WIP. Server operations
`cache_context`, `cache_verified` and `cache_receipts` are implemented. The
handoff records exact local domains, operation semantics, encoding and Android
ownership dependencies. There is no new portable/JNI API or PCGE format change.

No new versioned Windows/Pi group-attachment package is ready. The harness still
needs its capability-handshake and asynchronous-output sequencing updated before
the first complete Application test can pass. All saved integration work is
preserved, and development must remain stopped until the user resumes it.

## Branches and inspected interfaces

Current branch: `codex/native-unicode-emoji`, worktree
`C:/Users/Livel/.codex/worktrees/native-unicode-emoji/Create 2D Game`.
The shared inspection baseline is `e0940de`.

The inspected parallel task is **PeerChat Android groups and voice**, branch
`codex/android-groups-voice`, worktree
`C:/Users/Livel/Documents/Codex/peerchat-android-groups-voice`. At inspection its
HEAD was `e0940de`; its working changes were Android `MailboxExecution.kt` and
`MailboxIsolationTest.kt`. The new Kotlin `MailboxNative` boundary delegates to
the existing ProfileBridge/MailboxBridge APIs. It separates recipient-registration
failure from outbound/receipt servicing without changing encryption or routing.
Those changes belong to that branch and have not been copied or edited here.

Refreshed inspection: parallel HEAD `d67b0e5`, with a clean working tree. Its
`docs/ANDROID_GROUPS_VOICE_INTEGRATION.md` records the shared ownership boundaries.
Mailbox isolation is now committed as `d6c3a64`; `158d317` adds a build-only
Android Opus foundation, not a voice runtime. No new shared group C ABI or
attachment format is published there. Do not copy or reimplement those commits.
Its independent dev.156 APK remains a qualified mailbox candidate, not proof
that physical Android messaging or recipient registration is repaired.

That task plans secure Android group text/application integration followed by
voice. A concrete new shared group/voice C ABI was not yet published. Therefore
this milestone does not define competing JNI, Kotlin, group-session or voice
APIs. Before touching those boundaries, inspect its latest plan and commits again;
do not infer interface agreement from this initial inspection. No cross-task
message, merge or approval by the other task is claimed.

| Boundary | Dependency and integration rule |
| --- | --- |
| `portable/peerchat_core.h`, `portable/peerchat_core.cpp`, JNI bridges | Unchanged here. Android group/voice interface planning must precede any extension. |
| `group.hpp`, `group_key_manager.hpp`, `group_message_envelope.hpp/.cpp` | Unchanged here. Preserve signed ledgers, current-epoch PCGE admission and full-group-key handoff guards. |
| `device_key_manager.hpp`, `mailbox_protocol.hpp`, HPKE/provider interfaces | Unchanged here. Reuse signed recipient bindings and operation leases; never create replacement key custody. |
| Android mailbox application execution | Owned by parallel branch. No duplicate messaging repair here; root cause and physical acceptance remain qualified. |
| Historical/current roster evidence | Attachment decryption requires both verified contexts, exact original/current device-incarnation membership and authenticated cache expiry. A current roster alone cannot manufacture historical entitlement. |
| Windows Node/application transfer owner | Client integration is still pending. Reuse the existing TransferManager; do not create another chunk, journal, ACK or retry engine. |

## Modified shared files and new contracts

### Completion pass, 2026-10-15

| Area | Established baseline | Remaining production work |
| --- | --- | --- |
| Transfer engine | Negotiated chunks/windows, durable ACKs and explicit ciphertext purpose tested | Bind the same manager to cache RPCs and supplemental restart records |
| Encryption | Immutable blob, signed descriptor, HPKE packets and exact preparation recovery tested | Schedule bounded preparation and recipient finalization in Node |
| Server | Quota, expiry, leases, original/current admission and per-device packets tested | Return frozen authenticated roster context; exercise the complete client pathway |
| Windows UI | DM attachment cards and compact emoji composer accepted | Group draft, clipboard, one logical bubble, independent progress and controls |
| Recovery | Staging and isolated transfer recovery tested | Application jobs, durable imports, late-response rejection and end-to-end restart |
| Security | No old group-key release; revocation and missing evidence fail closed | Require verified historical ledger context for every incoming key packet |

The Android branch now has uncommitted additive GroupBridge/GroupApplication
work in portable core, reliable endpoint, relay requests, session transport and
group_text.hpp. Its manifest was re-read before this pass. This branch leaves
those owners untouched. Windows attachment controls use additive ui_bridge/Node
entry points; their local command records are not peer message formats.

Planned Windows-only exports are ui_group_attachment (UI actions/snapshot) and
ui_group_attachment_network (bounded authenticated RPC handoff). Managed code
retains existing pinned discovery TLS; Node retains attachment state and the
existing TransferManager. Replies carry the exact request and session generation.
The new cache_context RPC returns the original frozen roster plus current roster,
membership incarnations and immutable expiry. It never establishes signed-ledger
trust by itself. A recipient lacking verified historical evidence stays blocked.
Canonical attachment history is supplemental protected application metadata keyed
by signed descriptor/message identity, separate from per-device envelope bytes;
legacy PCGE history/deduplication and group text wire remain unchanged.

The attachment codec is checkpointed at `1bea3e0`; its gated server adapter and
durable-storage/security tests are checkpointed at `21403e4`.

- `group_attachment_descriptor.hpp`: additive `PublicDescriptor` parsing and
  manifest matching. Existing signed upload descriptor bytes stay unchanged.
- New `group_attachment_key_envelope.hpp`: bounded attachment-only HPKE codec.
  `keys::Authority` borrows verified original/current ledgers and roster snapshots,
  authenticated source/recipient membership incarnations and cache expiry.
  It establishes no trust, session, persistence, ACK or history authority.
- `server/AttachmentCacheDescriptor.cs`: source signature verifier for the new
  envelope; existing descriptor serialization remains unchanged.
- `server/AttachmentCacheAdmission.cs`: partial-class split only. New admission
  methods reside in `server/AttachmentCacheKeys.cs` and retain original/current
  group, device-session and recipient-key checks.
- `server/AttachmentCacheRuntime.cs`: initializes/reconciles a supplemental key
  table in the already-owned cache and includes it in cleanup. Existing history,
  account, mailbox and transfer-journal schemas are unchanged by this extension.
- `server/AttachmentCacheRequests.cs`: additive gated cache operations below.
  No discovery, relay, login, presence or normal mailbox protocol changes.

Supporting tests: `group_attachment_key_test.cpp`,
`test-group-attachment-keys.ps1`, `attachment_cache_keys_test.cs`, the existing
`attachment_cache_runtime_test.cs`, and its portable test project. Test-only
factories use isolated databases and generated keys, never production profiles.

## Protocol and storage additions

The attachment-only envelope uses domain `PeerChat/group-attachment-key/v1`,
signature domain `PeerChat/group-attachment-key-signature/v1`, and HPKE info
`PeerChat/group-attachment-key-HPKE/Base/v1`. It reuses the existing P-256 HPKE
suite and signed `device-recipient-binding/v1` bytes. The complete wire is bounded
at 8192 bytes; encrypted private attachment bodies remain bounded at 4096 bytes.

The signed header binds the existing signed upload descriptor, original source
and recipient membership incarnations, exact signed recipient key binding and
creation/expiry times. HPKE AAD binds that header. Only the encrypted body carries
the file key, nonce prefix, filename and plaintext digest. It carries no full
group epoch key. Expiration is bounded by the authenticated configured cache
deadline, not the unrelated ordinary-mailbox TTL.

New RPCs are `cache_recipient_keys`, `cache_publish_key`, `cache_key`,
`cache_key_ack`, and `cache_pending_keys`. They use the existing device-bound
authenticated cache RPC dispatch and remain behind its closed shipping gate.
Group-purpose key lookup does not relax friendship-specific mailbox lookup.
Only original/current eligible devices can receive a key. Missing registered
keys remain explicitly unavailable, not auto-created or auto-trusted.

The supplemental `attachment_key_envelopes` table stores immutable per-object,
per-device ciphertext, digest, key identity/generation and an independent receipt.
Writes are bounded, atomic and quota-checked. This receipt means only envelope
retention; it never advances a transfer offset, authenticates plaintext completion
or marks a message delivered. Retirement allows an exact existing envelope to
remain readable with its retained key; explicit revocation rejects it. Rejoin,
new membership and key substitution do not regain older attachments.

Key metadata follows its cache parent through `ON DELETE CASCADE`, including when
older maintenance SQL deletes an already-expired/unleased object without knowing
the new child table. This does not enable an older server's cache RPC support.
An existing pre-release table without the required ownership relationship is
rejected before lease/ledger cleanup, not automatically dropped or rebuilt.
Production rollback still requires the cache disabled on an incompatible binary;
never replace newer persistent data with an older backup merely to start it.

Recipient-binding discovery and envelope RPCs use canonical base64url. A binding
is public signed key metadata, not a registration or group-approval operation.

## Compatibility and package requirements

`5a4c2d8` adds `windows_group_attachment_keys.hpp`, a private Windows adapter for
protected preparation of an exact signed envelope before its first publication.
It reuses existing file/record protection and the attachment codec, and owns no
transfer, retry, session or group-message history. Profile/server-bound files
retain identical ciphertext across actual process restart, even when all HPKE
operations are disabled for the recovery test. Key rotation does not silently
replace an already prepared packet. Changed drafts, corrupt data, wrong-profile
copies and revoked/rejoined authority fail without overwriting saved files.
Its single file owner serializes preparation and its finite record limit bounds
storage, including abandoned partials. Verified expiration cleanup remains a
client-integration gate; reaching the bound must reject rather than fill disk.
This adapter requires no core C export/JNI or Android group interface change.
Any later application/history bridge remains subject to the checks above.

Its maintenance operation is Windows-private and caller-budgeted: remove
only expired, unreferenced packets after protected-record/signature/path ownership
verification. Preserve active staging references, corrupt/unrecognized files and
all actual attachment copies. It must add no timer, thread or transfer owner.
Checkpoint `29566b3` implements this bounded scan, verifying and deleting the same
opened file handle. Multiple hard links, corrupt protection and unknown files
are retained. The caller must supply successfully loaded active/staging object
references; this helper is not permission to clear metadata after a failed load.

Existing PCGE, DM/mailbox, archive and file-transfer wire formats are unchanged.
No numeric peer-message type, existing C export or JNI signature is changed.
Old clients must never receive a key envelope as ordinary group text. New
attachment operations require a compatible cache server and explicit capability
handling; unsupported servers fail closed rather than falling back to plaintext.
Existing Android group-text and voice work need not consume these APIs to build.

The new header is portable source, but actual Android provider/ABI integration
is not claimed or independently planned here. Later Android attachment support
must use that branch's approved application boundary and existing opaque key
provider. Missing historical evidence remains a recoverable authorization block,
not permission to bypass group-message admission.

Per-device key envelopes differ between recipients. They must NOT be inserted
as if they were the identical PCGE wire in legacy group-message history: existing
message-ID conflict checks intentionally reject different wires. The stable
logical attachment identity is the signed descriptor's group/message/object
binding. The eventual application owner must keep that canonical identity,
protected private metadata and per-recipient transport evidence distinct. Agree
its group UI/history projection with the parallel group-text interface before
adding shared application APIs; do not bypass deduplication or archive validation.

The parallel task attempted Android package number dev.156. Do not automatically
reuse that number for a different unified Windows/cache candidate. Allocate and
record distinct package versions when each branch is ready; preserve the dev.153
accepted packages and dev.155 diagnostic APK. No final cache package version is
allocated yet, and no live Pi installation is authorized by this manifest.

## Validated checkpoints and evidence

- `dc97469`: Android dev.155 diagnostics, not a proven messaging repair.
- `0607d4a`: dev.154 negotiated transfer pipeline, default 32 KiB by 1; physical
  acceptance of the new tuning remains separate.
- `ffc7d4d`: durable signed announcements and independent receipts.
- `10be929`: ownership check before cache schema changes.
- `446ab48`: bounded encryption preparation before protected key handoff.
- `09edaaf`: protected ciphertext recovery across process death and rename.
- `e0940de`: reproduced missing-key/stale-epoch rejection and protocol-review
  boundary; common baseline of both active worktrees.
- `72f9cc7`: initial parallel-interface inventory and integration boundary.
- `1bea3e0`: attachment-only native HPKE codec and actual provider/registry tests.
- `21403e4`: original/current group admission, immutable key-envelope persistence,
  quota/revocation/restart tests and pinned-TLS cache RPC integration.
- `3c4068e`: explicit canonical base64url discovery and actual TLS decode check.
- `5a4c2d8`: protected exact packet preparation, fresh-process recovery without
  crypto, key-rotation stability, single file owner and bounded storage.
- `8d97e69`: cascading cache-key ownership compatible with older parent cleanup
  SQL; incompatible pre-release schemas rejected without mutation.
- `c8701f2`: exact-packet recovery and cache schema compatibility manifest.
- `29566b3`: caller-budgeted expiration of verified unreferenced preparation
  packets; active, unexpired, corrupt, hard-linked and unknown files preserved.

Latest focused evidence: `build/tests/attachment-keys-d2075706b18a48d884a04d2d3b8bc807`.
The real HPKE/registry, exact fresh-process preparation recovery and managed
durable-cache interoperability suite passed for both cross-account and explicit
same-account devices. New maintenance assertions verify one-entry scan budgets,
protected expired references, unexpired records, hard-link refusal, corruption
retention and unchanged ciphertext/staging. Native compilation uses warnings as
errors; the managed fixture build retains 15 existing warnings and zero errors.

Current evidence includes actual shipping HPKE/CNG and protected-registry tests, native to managed
durable-cache to native decryption, authoritative key lookup/publication,
rotation/revocation/rejoin, independent receipts, restart and corruption rejection.
The full isolated cache quota/ownership/crash suite passes with the new table.
Pinned-TLS fixture operations publish keys, log the source out and allow an
independently authenticated original recipient to retrieve/ACK them. Its managed
server factory uses opaque signed payloads; actual HPKE is proved by the separate
native round trip, not fabricated from that server fixture.

Additional validation passed 16 concurrent metadata admissions under a reduced
quota, clean metadata rejection preserving completed ciphertext, expired-key
polling alongside valid envelopes, the existing server compatibility suite and
portable group-body/current-epoch regressions. ARM32 Linux compilation passed;
execution on a Pi is not claimed. An ARM32 assembly cannot be loaded into the
Windows test process; the shipping-gate check instead passed against a separately
built host-compatible shipping assembly, with no cache database created despite
an enabled configuration.

Component checkpoint hashes and any later interface dependencies must be added
before a package is represented as integration-ready. Production client dispatch,
durable transfer resume, receiver finalization and group attachment UI are still
pending. Cache enablement, physical throughput and end-user delivery are not
accepted by these component tests.
