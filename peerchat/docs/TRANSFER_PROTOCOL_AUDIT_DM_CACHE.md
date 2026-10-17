# Transfer protocol audit for the DM cache work

Date: 2026-10-16. Source: `codex/native-unicode-emoji`, production checkpoint
`bdd28e5`. This is a source audit and isolated test record, not a claim of measured
Pi throughput. The approved deployment and physical-pending boundaries are
recorded separately in `OFFLINE_DM_CACHE_DEV173.md`.

## Direct peer transfer and opaque relay

`transfer_manager.hpp` is the common Windows/Android transfer owner. `Options`
accepts 32 or 64 KiB chunks and windows 1, 4, 8, or 16. On a capable authenticated
route, `tickImplScoped` negotiates v2 and a fresh nonzero negotiation number.
Without that route capability, the sender falls back to v1: 32 KiB/window 1.
Active media restricts the sender's live window to one chunk. One bounded payload
operation per transfer per tick and transport output backpressure still apply.

Windows `Node::Impl` defaults at `node.cpp` use 64 KiB/window 16 and zero upload
and download speed limits. Android `portable/peerchat_core.cpp` uses the same
chunk/window defaults with 10 Mbps upload/download defaults. Android's existing
5 Mbps, 10 Mbps, custom, and unlimited choices remain in
`TransferSettingsSnapshot.kt` and `TransferSettingsScreen.kt`. Zero means no
application speed limit. This does not remove the common 256 MiB file-size bound
in `MaxFileBytes`; admitted ciphertext permits its exact AEAD overhead only.

`TransferManager::receiveImpl` uses cumulative `Resume(offset)` ACKs. A live ACK
must be within the sent high-water bound and, after the initial resume, at an
outstanding chunk boundary. The receiver writes in at most 32 KiB storage calls,
flushes, hashes, and advances its durable prefix. `tickImplScoped` saves the
journal before releasing ACK/control frames. Finish verifies the complete hash
and commits the destination before Complete. Restore rehashes the source or
retained receiver prefix before resuming; wire negotiation is not persisted.
Old negotiations cannot advance a new authenticated route.

`protocol.hpp` bounds application packets at 70,000 bytes and byte fields at
65,536. The server's `DiscoveryServer::ServeRelay` authenticates and pairs the
endpoints, verifies relay session binding and continuing authorization, then
forwards opaque frames. Relay payload pieces are at most 64 KiB, separate from
application chunk boundaries. Android `ReliableEndpoint::takeOutput` drains at
most 65,536 encrypted stream bytes at a time. The relay does not parse the file
protocol or own file ACKs, hashes, or resume state.

## Cache adapter: separate protocol and bottlenecks

`windows_group_attachment_application.hpp`, `Application::send`, translates
local TransferManager frames into bounded authenticated `cache_status`,
`cache_append`, `cache_complete`, and `cache_progress` JSON RPCs. These RPCs are
not peer TransferManager v2 on the network. The upload adapter synthesizes an
ACK only after the server returns the expected durable end offset.

Before this change, `Application::createOffer` synthesized a v1 receiver Offer
and `Application::tick` requested at most 32 KiB per `cache_read`, despite a
64 KiB server limit and adapter parser. Each read waited for the durable local
ACK and its `cache_progress` response before another read. Simply requesting
64 KiB would violate the v1 receiver's chunk-size check.

The narrow optimization synthesizes a valid local v2 Offer with 64 KiB/window 1,
using the newly allocated admitted cache route as the negotiation token. The
existing scoped capability is established before that Offer. Downloads now ask
for `min(65536, remaining)`; each still waits for durable ACK/progress. Restored
jobs negotiate afresh through the existing code and their saved format is
unchanged. Server RPC schema, transfer tuning, media window restrictions,
concurrency, retry ownership, and persistence authority are unchanged.

`desktop/GroupAttachmentExecution.cs`, `Service` and
`PumpGroupAttachmentNetwork`, still execute one borrowed synchronous RPC at a
time. The pump services up to 16 requests or 20 ms per task before returning to
its normal scheduling. A native window of 16 does not create 16 concurrent cache
RPCs. The optimization halves the normal read/progress pair count, not latency
or fsync requirements within a pair.

`server/AttachmentCacheRequests.cs` already accepts 64 KiB reads/appends in both
group and DM RPC branches. `AttachmentCacheRuntime::Append` requires contiguous,
32 KiB aligned chunks and identical duplicate contents; replies follow a
reserved SQLite mutation and commit. The cache index uses `synchronous=FULL`,
`journal_mode=TRUNCATE`, and exclusive locking. `Read`/`ReadChunk` reconstruct
bounded ranges and verify stored chunk digests. Remaining costs include the
runtime lock, commit/fsync per append, base64/JSON copies, read digest work,
native scheduling, and sequential RPC round trips.

Concurrent requests, batched ACKs/progress or commits, larger network frames,
and different storage durability are separate optional changes requiring their
own evidence. None is needed for this compatible 64 KiB improvement.

## Validation status

The enhanced `tests/test-windows-group-attachment-application.ps1` fixture
uses production Application, TransferManager, native HPKE, managed DiscoveryClient,
TLS, and the cache server. New checks inspect actual request sizes and short
tails, local v2/window 1, active media, a lost read response, a lost durable
progress response, restart and stale reply rejection, and migration from a
locally established old v1 receiver. Existing coverage includes sender offline,
signed epoch/membership admission, accepted-recipient behavior, exact Unicode
plaintext, restart during preparation/upload/download/finalization, retained
verified copies, safe exports, and signed independent receipts.

The fixture reports actual read/progress RPC counts and a calculated previous
32 KiB read count for the same ciphertext. It injects restarts and adds host
projection calls for assertions; it is not a controlled throughput benchmark.
No Pi throughput, real-device Android, or deployment result is claimed here.

Before any DM deployment, integration owners must also complete the relevant
pipeline/budget/restart/control replay/cache scope tests, authenticated Windows/
Android interop, group regression, server cache/storage gates, DM audience and
recipient isolation, lost-response/restart tests, and final candidate build.
Only results explicitly recorded below count as run in this audit.

Run on 2026-10-16:

- `test-transfer-control-replay.ps1`: PASS control replay, every 32/64 KiB and
  1/4/8/16 window pipeline combination, v1 fallback, capability/stale negotiation
  rejection, and protected restart regressions. Evidence:
  `build/tests/transfer-control-regressions-813a29cfe2c54175a1f192fa4b1c4781`.
- `test-transfer-cache-scope.ps1`: PASS exact plaintext/ciphertext size bounds,
  group/DM/archive scope isolation and production encrypted-object round trip.
  Evidence: `build/tests/cache-scope-d2e85d80db164d72917cf7e83b5e06df`.
- First Application fixture compiled, but stopped at authenticated capability
  probing before attachment preparation or download. Evidence:
  `build/tests/cache64/application-89c4354673a24f8fbcb26275361c7110/results.log`.
  This is not a passing download result; the subsequent established-drive run
  below is the passing integration evidence. The first-run capability issue
  was not attributed to the download change.
- `test-windows-group-attachment-application.ps1` on its established isolated
  `F:` test drive: PASS the full production Application/TLS/cache integration,
  including all new 64 KiB checks, short tail, lost read/progress responses,
  stale-response rejection after restart, a v1 receiver restart, and active
  media with local window 1. Evidence:
  `F:/PeerChat-isolated-cache-tests/application-cad3bfaf492148498b1afc34589a8855/results.log`.
  For 2,098,213 ciphertext bytes, a clean 64 KiB download requires 33 read RPCs,
  versus 65 at the previous 32 KiB request size. Actual recipient read counts
  were 34 with a deliberately lost read response and 33 with a deliberately
  lost durable progress response; both used 35 progress RPCs including injected
  restarts. Both recovered exact plaintext and safe exports. These are RPC
  count results, not Pi or general network throughput measurements.

## Windows DM adapter integration

`windows_dm_attachment_adapter.hpp` supplies the common controller's Windows
filesystem callbacks. Ciphertext staging uses a separate DM token domain and
private directories, with the existing FileSink inode/volume checks. Approved
source recovery uses the existing protected snippet/file source-token reopen
and checks original size/name. A recorded stage resumes its exact owned partial;
an orderly-discarded partial can be recreated only after validating its DM
domain, directory and exact ID-owned filenames, with both names absent and an
exclusive create. No published file is replaced by this recovery path.

`node.cpp` wraps the existing group/ordinary journal in `DirectCacheJournal`
before constructing TransferManager. Purpose-2 records never reach the legacy
journals. A separate ephemeral verified plaintext descriptor feeds the existing
download-save coordinator; it is not a peer transfer and is never journaled as
one. Download acceptance keeps one existing Save As dialog; export occurs only
after ciphertext/AEAD/plaintext verification and internal durable completion.

The existing `ui_file_transfer` ABI is reused:

| Operation | Meaning |
| --- | --- |
| 20 | JSON DM snapshot, including `dmState`, upload and notice flags |
| 21 | Start: peer, recipient account in `id`, original path |
| 22 | Accept/retry export: attachment ID, existing base64 download selection |
| 23 | Cancel attachment ID |
| 24 | Base64 `PeerChat/DMCacheSession/v1` account/generation/server record |
| 25 | Next bounded base64 `DMCacheCommand/v1`, or empty |
| 26 | Deliver bounded base64 `DMCacheReply/v1` |
| 27 | Deliver bounded base64 `DMCacheFailure/v1` |
| 28 | Read-only protected local history page, opaque cursor in `id`, at most 32 decoded records |

`desktop/DmAttachmentExecution.cs` translates the common binary transport record
to one borrowed authenticated RPC at a time. Native retains all retries,
immutable notice bytes and ACK authority. Account conversations use cache
preflight without requiring an active peer connection. Existing verified native
identity pins and account/device/key evidence are still required; the adapter
does not establish trust from server availability. Legacy transfer fallback is
restricted to explicit incompatibility and a live verified native route.

The first real DM Application/TLS run compiled and reached upload but correctly
exposed the original client-restart lease defect:
`DM job paused: cache_upload_already_owned`. Evidence:
`F:/PeerChat-isolated-cache-tests/dm-application-442ac39d8c7340588efb02d1dd2e0cd3/results.log`.
The user subsequently approved scoped source, lease and per-job recovery work.
Windows source/stage callbacks have been wired to that common API. The stronger
real-TLS rerun passed:
`F:/PeerChat-isolated-cache-tests/dm-application-1de00200cdad4b178447472d5eafc122/results.log`.
It exercises a restart before preparation, an orderly-discarded mid-seal stage,
lost append response and upload restart, two independent offline recipient
devices, sender logout and server restart, lost durable progress response and
receiver restart, duplicate replies, exact Unicode/binary verified plaintext,
safe export, no early notice ACK, signed file receipts and reverse delivery.
That first run waited for forward signed receipts and reverse verified plaintext;
the additional mandatory completion/receipt test below closes its narrower
lost-completion and reverse receipt-aggregation coverage gaps.

The same DM fixture also passed against the actual production server process
built with candidate flags and without `PEERCHAT_CACHE_TESTS`:
`F:/PeerChat-isolated-cache-tests/dm-application-f864e64e0579459a866d857943d64138/results.log`.
It used 18 append RPCs and 27 read RPCs across the injected recovery scenarios.
The strengthened actual-process candidate rerun also passed, now deliberately
losing the `cache_dm_complete` response and immediately restarting the client,
and requiring signed verification receipt aggregation to complete in both
directions:
`F:/PeerChat-isolated-cache-tests/dm-application-53d7f9e0bcd94df9a5eab9c558bbd6d3/results.log`.
Its required lost-append/lost-complete/restart flags were all observed; append
and read totals remained 18 and 27. No protocol or retry policy was changed.
The group Application regression passed against that same candidate executable:
`F:/PeerChat-isolated-cache-tests/application-922da605dfd84247b7e391fc679caf3f/results.log`.
Its 64 KiB read counts remained 34/33 for the two recipients versus 65 clean
reads at the former 32 KiB size. These are bounded isolated loopback tests;
neither timing nor RPC counts establish physical Android/Pi throughput.

## Windows boundary review and corrections

The recovery review checked current account/server scope, retained leases,
source/stage token ownership, preflight identity pins, request-generation replay
checks and verified export. Three local-operation checks were tightened:

- Operation 22 now requires the common controller's current-scoped
  `completedLocation(id)` to equal the exact verified plaintext token before
  retrying an export. Retained adapter records alone do not authorize export
  after account/server changes.
- Operation 23 now requires the attachment to appear in current-scoped
  snapshots before canceling. The shared controller also owns its own scope
  validation; platform callers must not rely solely on UI filtering.
- A `legacy:` operation-21 request now requires native `capabilityState()==2`
  (explicit unsupported capability), as well as the existing authenticated
  friend mapping and live verified route. Pending negotiation, network errors,
  storage failures and unavailable sessions cannot request a downgrade.

The actual Application fixtures exercise the common controller, Windows adapter,
protected journals and production managed executor. They do not invoke the
Node `ui_file_transfer` ABI operations 20–27. Existing Node ABI integration
coverage is archive-specific; do not label it a DM ABI runtime regression.
Strict production Node compilation passed after these three checks. The final
full production build and physical-device interoperability remain separate gates.

The integration owner confirmed the intended distinction: a durable export
already selected by the user may finish offline or after logout; historical
verified local copies are not remotely revoked. New manual exports still require
the current operation-22 context check. `plaintextRecords()` is populated only
by the controller's `verified` callback, after Finalizer verifies complete
plaintext/ciphertext hashes and AEAD and commits the owned plaintext sink.
Neither ciphertext staging nor an unverified partial enters that collection.
The coordinator independently reopens and hashes its retained plaintext token
before constructing OutputExport, and uses only the persisted selected target
and overwrite approval. This retained local-copy intent grants no network or
new audience authority; no coordinator behavior was changed.

The same owned-absent recovery rule now covers an unfinished plaintext Finalizer
partial discarded during orderly shutdown: validate the DM directory, exact
attachment-owned partial/final names and complete token, then exclusively create
only if both names are absent. Existing partials still require their recorded
inode; existing full plaintext is re-decrypted/compared, never replaced. The
actual candidate fixture passed a deterministic mid-Finalizer restart and a
post-verification restart preserving committed plaintext:
`F:/PeerChat-isolated-cache-tests/dm-application-77a517bc3e484d289e2df0e3c9cd40be/results.log`.
The first new injection attempted to detect open-file length through filesystem
metadata and did not observe its required injection flag; it was replaced by a
deterministic third-verifying-step injection with an owned-partial existence
assertion. A subsequent diagnostic-message compile error was corrected before
the passing run; neither failure is counted as passing evidence.
An additional direct replay of the committed-file callback initially ran while
the adapter's read-only verified-source handle was still open; the exclusive
FileSink reopen correctly failed closed. The corrected test runs at the actual
restart point, before reopening the verified reader, and waits for fresh cache
capability registration before its subsequent reverse send. This strengthened
actual candidate run passed, including the committed-file comparison replay:
`F:/PeerChat-isolated-cache-tests/dm-application-7d69dbfff8824c1fb270ed241a962be9/results.log`.

## Protected local DM history paging

Native operation 24 records a separate profile-protected local read context only
after validating the authenticated account/session. Operation 28 reads that
saved account/server context and `Application::historyFor`; it never calls
`session`, negotiates capabilities or grants network authority. Logout retains
this historical read view. New manual exports retain the separate current-scope
operation-22 guard. The existing chat “Load older messages” action also pages
these attachment records, retaining loaded older bubbles across current-snapshot
refreshes. Profile/account/server changes reset cursors and invalidate stale
asynchronous results; no HistorySync or cache network schema changed.

All production Desktop C# sources compiled after paging integration. The focused
`tests/dm_attachment_history_projection_test.cs` passed with 608 rows, a 32-row
current refresh and a repeated older page, proving no projection deletion or
duplicate bubbles at the former 32/512 boundaries. Evidence:
`C:/Users/Livel/AppData/Local/Temp/PeerChat.DmPaging-d7d9ebd996d24987a1028240128dde0d`.
Native protected page-chain/restart/scope/CAS tests are maintained separately by
the common-controller owner; this projection check is not a Node ABI runtime test.

DM bubble progress is logical plaintext progress, while TransferManager counters
measure confirmed encrypted payload. The current local snapshot does not expose
an exact reliable rate mapping, so the bubble explicitly shows encrypted-payload
throughput/duration as unavailable and does not fabricate zero current speed or
an ETA. Existing native transfer diagnostic counters remain available; no new
instrumentation, persistence or wire fields were introduced for this UI fix.
All-source Desktop compilation and the projection regression passed again with
an assertion rejecting fabricated zero current speed/ETA for DM cache rows:
`C:/Users/Livel/AppData/Local/Temp/PeerChat.DmPaging-6f48656cb0d244629809fa8e9e24521a`.

## Permanent cache-object loss

The common controller now treats exact `cache_attachment_expired` and
`cache_attachment_unavailable` replies as terminal network failures, except for
the existing initial unadmitted status-to-admit discovery path. Unverified jobs
become failed with the specific reason; verified local copies stay complete and
readable. No receipt-published flag is fabricated. Pending requests/routes are
stopped, terminal jobs no longer consume active-work slots, and queued local
copy intents remain independent. Protected source/ciphertext/plaintext records
are not canceled or deleted. Other rejection classes keep their existing retry
policy. A failed local verified-output callback also preserves the terminal
cache reason, rather than restoring network retry behavior. No new serialization
or protocol fields were added.

The complete `test-dm-attachment-body.ps1` contract suite passed after this fix:
`build/tests/dm-attachment-body-95115ae55a7d4a7f9768f7d2fc1d0224`.
The focused additions reject 40 expired/unavailable jobs without filling the
32 active slots, retain every protected job and original source, preserve
transient quota retry, and retain a verified complete readable copy through an
always-failing local output callback and restart without further network work.
The suite also passed protected Storage job/index CAS, scope, paging, legacy
compatibility and DirectCacheJournal isolation regressions.

## Post-durable mailbox wake gate

The final fixture adds real production recipient event-channel subscribers and
requires `mailbox_ready` after valid stored publications. It also modifies a
publication signature and requires rejection without a new mailbox hint. During
this notification-specific run the recipient download controllers remain
inactive during upload, but their event subscribers are online; the earlier
no-event-subscriber runs remain the fully offline recipient evidence. The fresh
4.0.2-dev.173 actual-process candidate passed this notification gate and all
completion/restart/verified-file/receipt checks:
`F:/PeerChat-isolated-cache-tests/dm-application-b5794fed7f044b88b843eb9954b2ec6d/results.log`.
It used `build/dm-cache-wake-server-validation/Server/PeerChat.Server.exe`,
without test-only server flags. No physical device or Pi throughput claim follows
from it. All owned fixture processes exited after the run.

The separately identified post-admission engine-slot defect was reproduced with
the real Application/TransferManager: repeatedly expiring an admitted object's
append exhausted the independent four-transfer bound (`File transfer limit
reached`). Evidence fixture:
`build/tests/dm-attachment-body-335c62c758664847a372d99ac157d95f`.
The earlier 40-error test rejected before engine admission and did not cover this.

Before correcting this boundary, the shared integration manifest and dev.167
Windows/Android interface report were rechecked, together with the other
`android-messaging-integration` worktree's `SHARED_PEER_RUNTIME.md` and
`ANDROID_MESSAGING_VALIDATION.md` at read-only checkpoint `5481933`. That older
worktree has no transfer engine or competing retirement API; its native
owner/JNI boundary remains unchanged. It was not edited or merged. The proposed
dependency is additive internal C++ `TransferManager::retireDirectCacheCiphertext`
called only by the common DM controller after a permanent cache-object error.
Windows Core and Android native libraries must be rebuilt together with the
matching header; no C/JNI signature, peer/server packet or journal format changes.
The current parallel Android project's manifest and source were also rechecked
read-only at `ac7ab34` in
`C:/Users/Livel/Documents/Codex/peerchat-android-groups-voice`: clean worktree,
unchanged scoped enrollment/JNI boundaries and no competing retirement method.

`retireDirectCacheCiphertext(peer,id,reason)` accepts only an exact purpose-2
record, matching peer/ID and the two permanent object-loss reasons. It persists
existing Failed state plus the retained token before closing durable handles,
never discards a partial, queues no Cancel, and never promotes completion.
Failed persistence restores the prior state/error/location while leaving live
handles and pending controls intact. Existing Complete records are unchanged.
The controller retires the engine before recording its own terminal state. A
one-time local startup reconciliation also handles exact protected terminal jobs
whose older unshipped journal still says active; it does not establish a session
or read network authority and considers at most the existing four active records.

Focused retirement/application tests passed:
`build/tests/dm-attachment-body-40717bf7fef7461fba297e92231645af`.
They preserve a receiving partial through failed-save rollback and restart without
reopening it or emitting Cancel, reject wrong peer/PeerFile/group-purpose calls,
preserve Complete records, expire eight post-admission jobs with client/engine
restarts, retain their ciphertext and then successfully upload a new object.
The final frozen rerun passed including the explicitly seeded older
terminal-job/live-engine startup case before any network session:
`build/tests/dm-attachment-body-00e2a75e44f34e87accdb9c34ec10cb9`.
All owned tests exited; production headers and focused fixtures were frozen for
the matching Windows Core/Android native rebuilds. The earlier real TLS wake
fixture is recorded above; this local failure-path correction introduces no
network change and is not represented as physical device acceptance.

Windows production Node syntax passed strict `-Wall -Wextra -Werror` compilation
after recovery callbacks were added. All production Desktop C# sources also
compiled, excluding the repository's test-only entrypoints. These checks do not
replace the explicit final candidate build or real-device interoperability.
