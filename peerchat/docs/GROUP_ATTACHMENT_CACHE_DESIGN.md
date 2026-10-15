# Encrypted group attachment cache design

Status: architecture decision and implementation gates, not production acceptance.
The existing TransferManager remains the only client chunk, ACK, hash, journal,
retry and completion owner. No live Pi deployment or Android installation is authorized.

## Component decision

Use the existing PeerChat server and SQLite dependency with a dedicated ciphertext
cache database and a narrow authenticated adapter. Do not add NATS, tusd, another
listening port or a new client transport merely to obtain storage.

NATS Object Store provides chunked objects on JetStream with object integrity
verification. It requires a JetStream-enabled nats-server. ARM7 packages exist.
It would still require PeerChat device/epoch/revocation admission and a private
protocol adapter; its service, client library, operational limits and persistence
configuration would be additional responsibilities. No measured RAM or throughput
claim is made for this uninstalled candidate.
[Object Store](https://docs.nats.io/learn/object-store/),
[ARM7 release assets](https://github.com/nats-io/nats-server/releases).

tusd is an MIT-licensed Go implementation of HTTP resumable uploads. Its local
backend stores data and separate metadata files. Hooks support validation, but
PeerChat would still need request-by-request device authorization, cache expiry,
quota reservation and controlled downloads. Adopting its HTTP protocol would
duplicate the existing client transfer machinery. Do not expose a tus upload URL
as a bearer substitute for current group authorization.
[tusd](https://tus.github.io/tusd/),
[local storage](https://tus.github.io/tusd/storage-backends/local-disk/),
[hooks](https://tus.github.io/tusd/advanced-topics/hooks/).

SQLite is already used by both production server builds, including actual ARM32
validation. A separate cache database avoids migrations to accounts, identities,
mailboxes and history. Transactions can commit ciphertext chunks and their durable
offset together. The adapter must remain a bounded storage/admission component,
not a second client TransferManager or a general object-storage framework.

## Transfer extension

Legacy FileTransfer version 1 remains byte-for-byte 32-KiB chunks with one
outstanding chunk. Windows advertises capability 8192 only for the additive DM
pipeline. A capable sender requests version 2 only for a nonstandard chunk/window.
The immutable offer adds selected chunk size and credit limit; every v2 frame
includes a negotiation identifier. Supported sizes are 32768/65536 and windows
1/4/8/16. The reliable stream supplies ordering; cumulative durable offsets are
sufficient, so no selective bitmap or out-of-order buffer is introduced.

The sender retains at most sixteen outstanding end offsets, not whole files.
Prepared/submitted data does not advance confirmed progress. A resumed sender
rehashes its source and learns the receiver's durable prefix before transmitting.
Delayed ACKs cannot rewind an active generation; old negotiation ACKs cannot affect
a new one. Durable journal formats remain unchanged; optional protected tuning
lives separately and cannot authorize bytes. Archive transfers remain on v1 until
their bounded wrapper explicitly supports another version.

A shared token bucket accounts for every outgoing payload across all transfers;
an individual bucket cannot bypass it. The maximum initial/bucket burst is 64 KiB.
Round-robin service and existing reliable-output backpressure bound bulk work.
Known active DM/group media conservatively limits actual bulk credits to one,
without changing the negotiated maximum or media pacing. This is not a guarantee
against physical CPU, disk or network contention.

## Encryption and immutable object binding

Peer TLS ciphertext is connection-specific and MUST NOT be cached or duplicated.
The cache must receive an application-encrypted immutable blob instead.

Use the existing AES-256-GCM provider with a fresh random attachment key, domain
separation and unique per-block nonces. Persist the encrypted staged object before
upload; never reencrypt changed plaintext under the same key/nonce after restart.
Reuse group message encryption and signatures to deliver the attachment key and
encrypted manifest only to the intended authorized epoch. Plaintext filename,
content type and plaintext digest stay inside that encrypted manifest.

Canonical associated data must bind version, attachment ID, sender device/account,
group ID, roster/key epoch, block index and plaintext length. The signed public
upload descriptor binds the immutable ciphertext digest and length to the same
identity/group/epoch. The Pi may authenticate that descriptor but receives neither
the plaintext manifest nor the attachment key. Recipient decryption and final
plaintext SHA-256 verification are separate from server ciphertext delivery.

The 256-MiB plaintext limit is not permission to omit AEAD tags or integrity data.
Ciphertext overhead needs an explicit bounded cache-transfer scope; never raise
the legacy DM limit or silently pass an oversized blob through an old peer.

## Authorization and retention

At admission, verify the authenticated device session, signed descriptor,
authoritative group roster and current epoch. Freeze the original eligible device
set under that roster mutation lock. Every future download must pass BOTH original
eligibility and current non-revoked group/device membership. A new member or
replacement device does not inherit cached historical access. No key or grant is
created automatically. Already decrypted/downloaded data cannot be revoked.

Recipients have separate durable cursors and end-to-end verification receipts.
Sender disconnection after durable upload does not prevent eligible recipients
from downloading. One recipient's failure must not reopen another's completed copy.
Transport connection or server ciphertext completion is never labeled plaintext
verification. Keep one logical attachment message across attempts.

Defaults: 48-hour complete retention, six-hour incomplete inactivity expiration,
10-GiB total cache budget, 512-MiB account and group quotas, 256-MiB plaintext
file maximum. Cache capacity and retention are administrator configuration, not
compiled policy ceilings; capacity uses checked 64-bit arithmetic even on ARM32.
Offline clients do not extend expiry. Expired access fails explicitly.

Periodic cleanup removes expired objects first. Admission under pressure may
evict the oldest eligible completed objects before normal expiry. Never evict
active protected transfer leases or overwrite immutable ciphertext. If sufficient
space cannot be reclaimed, reject admission. Eviction invalidates future cache
downloads, not recipients' completed local files. Reducing a quota stops new
reservations and reclaims eligible objects; it must not forcibly delete leased
objects or silently pretend the smaller quota is already satisfied.

The policy's bounded candidate page is not a total cache-object ceiling. The
durable ledger must include all unpaged object charges in the snapshot totals
and page candidates in global expiry/age order under its admission gate. A
larger volume must not require loading an unbounded object index into RAM.

Reserve ciphertext, partial/temporary files, metadata and transaction overhead
atomically before admission. Recheck actual cache-filesystem free space and
maintain max(2 GiB, 20 percent of that filesystem) by default; administrators can
configure a stricter reserve. Effective capacity is reduced when the configured
volume cannot support the requested budget. Concurrent reservations cannot reuse
the same free capacity. Restart reconciles reservations and owned partials.

On 2026-10-14, read-only inspection reported armv7l/32-bit, ext4 on
/dev/mmcblk0p2 mounted at /, filesystem size 125421432832 bytes, available
112239681536 bytes and /opt/peerchat-test allocated usage 1450319872 bytes.
RAM was 3967205376 bytes total and 3570376704 bytes available; swap total
536866816 bytes. These are observations, not a permanent capacity assumption.

The configurable cache directory must be bound to an explicitly initialized
cache instance and the expected mounted filesystem. Missing mount, changed
storage identity, missing marker or an unavailable directory disables cache
admission; do not create a fallback directory on the OS filesystem. The cache
directory may be on SD, USB SSD or another mounted volume. Moving it requires
explicit quiesce, consistent copy, integrity/permissions checks and config switch,
with the old copy retained for rollback. Never automatically move/delete a cache.

### Persisted configuration and safe changes

`ServerConfiguration.AttachmentCache` is shared by the daemon and existing Windows
admin interface. Its fields are `MaximumBytes` (10737418240 by default),
`RetentionSeconds` (172800), `PartialIdleSeconds` (21600), `MinimumFreeBytes`
(2147483648), `MinimumFreePercent` (20), `Directory`, `ExpectedFilesystem`,
`ExpectedMountPoint` and `InstanceId`. All byte counts and retention values are
signed 64-bit integers; clients cannot set them. There is no 10-GiB ceiling.
The per-file 256-MiB plaintext bound remains a separate transfer validation rule.
Changing this configuration never creates, copies, moves or deletes cache data.
The admin panel explicitly labels these as staged settings, not an enabled cache.
No runtime endpoint is enabled by filling them in.

An absent cache section in an older configuration receives defaults. Explicitly
null or invalid sections fail validation rather than silently resetting limits.
Apply settings at a controlled server restart, not a partially applied hot reload.
Before enabling runtime support, the storage probe must bind the configured mount
to stable filesystem identity (for example UUID, not only `/dev/sda1`) and the
cache-instance marker. Recheck that binding for admission and writes. Keep writes
anchored to validated storage handles so a check/unmount race cannot redirect them
to an ordinary directory on the OS volume. No automatic fallback or mkdir-on-error.

On quota reduction, reconcile actual allocated bytes, metadata, temporary objects
and durable reservations under one admission gate. Reclaim expired eligible
objects first, then oldest eligible completed objects. If protected leases still
exceed the new quota, report over-quota/draining and reject new uploads until the
budget is satisfied; do not revoke a live lease. Report configured and effective
capacity separately. Existing expiry timestamps remain immutable when retention
changes; the new retention applies to subsequently completed uploads unless an
administrator explicitly requests a separately validated expiry migration.

### Explicit relocation procedure (implementation gate; do not run on live Pi)

1. Record the old config, cache instance, filesystem identity, quota, schema and
   binary version. Verify a restorable consistent backup of the cache index and
   ciphertext; keep account/history databases, TLS keys and TURN settings separate.
2. Validate the new mounted volume, stable identity, service ownership, restrictive
   permissions, 64-bit offsets and actual available-space reserve. Refuse an
   unmounted mountpoint. Do not infer media type or capacity from its pathname.
3. Quiesce cache admission/downloads, drain or explicitly cancel leases, then stop
   the cache owner. Preserve journals/reservations for reconciliation. Never copy
   a live SQLite database by copying its main file alone.
4. Copy the consistent owned cache into a new empty staging directory on the new
   volume. Do not move or overwrite the old cache. Verify every immutable object
   length/digest, index consistency, reservation accounting and permissions.
5. Bind the copied instance to the explicitly approved new filesystem, update the
   configuration, and restart the same compatible binary. Verify recovery, quota,
   read authorization and a download before reopening admission.
6. If checks fail before new writes, stop, restore the old configuration and
   restart against the untouched old cache. After new writes, never roll back to
   a stale index blindly: quiesce and reconcile a consistent current copy first.
   Retain both copies and rollback records until separately approved cleanup.

Production migration tooling must implement and test these gates before exposing
a migration action. The current candidate offers no migration or cache-enable
button and has performed no migration or live server deployment.

## Storage and implementation gates

Require an absolute, service-owned restrictive cache directory; reject symlinks,
reparse points, unsafe parents and arbitrary client paths. Names are server-owned
opaque IDs. SQLite FULL durable transactions must atomically commit ciphertext,
prefix, immutable descriptor and quota state. Actual metadata, partial, temporary
and reservation charges count toward the budget, not just completed payload.
Startup reconciles interrupted transactions; cleanup is bounded and touches only this cache, never local user
files, accounts or history. Filesystem deletion is not secure physical erasure.

Before production exposure, tests must prove correct signatures, epoch and device
mapping, replay/conflict rejection, original-recipient/current-membership
intersection, receiver plaintext verification, durable upload/download resume,
quota/disk failure, expiry, concurrent recipients, cancellation and server restart.
Include quotas above 10 GiB on ARM32 without allocating that much test data,
quota reductions, active leases, oldest-eligible eviction, mount disappearance,
storage identity mismatch and explicit non-destructive migration/rollback.
Run real ARM32 isolated tests as well as Windows fixtures. No backend may report
supported group attachments before those paths and the client adapter pass.

## Current validation checkpoint (2026-10-14)

### Resumed cache runtime and authenticated request validation

The runtime now stores ciphertext chunks and durable offsets in the same FULL
SQLite transaction, with anchored directory handles, reserved capacity, bounded
leases, expiration, pressure eviction and restart recovery. Signed admission
uses the authoritative group roster and freezes membership incarnations;
revocation and rejoining cannot recover an old object's eligibility. Actual
process termination before upload and cleanup commits preserves the previous
durable state. Sixteen concurrent admissions under a 20-MiB fixture quota accept
four and reject twelve without exceeding the reservation ledger.

Pressure cleanup previously tested whether the existing cache fit, rather than
whether the incoming reservation would fit. It now reclaims enough eligible
completed data for that reservation while preserving active transfer leases.
This is a demonstrated admission defect, not a physical throughput claim.

The new bounded cache request adapter uses existing authenticated RPC and
requires its existing device-proof-bound session. Account-password-only tokens
cannot use cached historical access merely by naming another owned device.
The normal mailbox, relay and authentication behavior remains unchanged.
Canonical unpadded base64url keeps worst-case 64-KiB ciphertext chunks within
the existing 100000-byte JSON frame limit; standard base64 could exceed that
bound after JSON HTML escaping. Padded, noncanonical and oversized encodings
are rejected without advancing durable progress. Cache response version is 1.

Isolated real pinned-TLS tests pass a signed 3-MiB upload, reconnect after a
durable prefix, completion and exact ciphertext reads. Distinct same-account
devices without group authorization, mismatched session devices and legacy
unbound tokens are rejected. The observed 1130-ms append interval covers
3,081,748 ciphertext bytes after reconnect, including JSON and FULL SQLite
commits, but excludes encryption, the first chunk, connection establishment
and any physical network. It is not a Windows/Android or Pi throughput result.

Both shipping server builds include the adapter, but production readiness is a
separate compile-time closed gate. Configuration defaults to `Enabled=false`;
even explicitly enabling it in a shipping build returns `cache_feature_gated`
without creating cache storage. The independently compiled shipping assembly
passes that test over real TLS. Test-only builds exercise the open path. The
existing presence worker performs bounded periodic cleanup; no new worker or
retry owner is introduced. Storage closes after server workers drain.

Client TransferManager integration, protected immutable staging/manifest
recovery, offline announcements, group attachment UI and plaintext completion
receipts remain required before opening this gate. The earlier dev.154 results
below remain the negotiated DM pipeline baseline, not group feature acceptance.

Protected immutable staging is now available through a bounded supplemental
object in the existing profile-protected store, with no message schema or
legacy transfer-journal format change. It retains the original Unicode name,
attachment binding, AES key/nonce prefix, digests and opaque source token.
An existing object's key, nonce, binding, token and expiry cannot be rewritten
in place. Actual process exit and reopen recover exact plaintext; stale writes,
device substitution and corrupted protected data fail closed. Source ciphertext
must still be reopened and verified before resume. A crash between ciphertext
sealing and staging commit is not an announced or uploadable attachment; complete
intent/orphan recovery remains an integration gate.

The existing TransferManager now has an explicit local ciphertext purpose and
manifest-bound entry points. Its maximum is the unchanged 256-MiB plaintext
limit plus the exact bounded AEAD overhead; ordinary offers and archive fetches
remain capped at 256 MiB. Peer frames cannot select this purpose. Control frames
cannot cross between purposes or conflict with the caller's expected immutable
metadata. Default route callbacks refuse cache entries; the single owner must
select an admitted route through its existing scoped tick callback.

Legacy journals explicitly lack support for this purpose and cannot receive
cache entries. A separately protected resume-state adapter is still required
before production use. Windows file sources can reopen the additional overhead
only with the explicit cache purpose. A sparse maximum-size fixture verifies
that boundary without allocating or transmitting a 256-MiB payload. A nonempty
encrypted-object round trip uses the same TransferManager hash/credit/ACK and
completion paths, followed by separate exact plaintext verification. Existing
legacy restart, all eight pipeline combinations and bandwidth-budget tests pass.

The native public descriptor serializer now matches managed verification:
native device signing, managed signature/digest checks, FULL cache upload,
server-storage restart and read, then native decryption with the original
protected key all pass for a nonempty fixture. Descriptor tampering is rejected.
The public descriptor excludes the filename, plaintext digest, key and nonce
prefix. This storage/codec test is distinct from the authenticated TLS/roster
fixture; it is not a claim that the client upload/fanout adapter is connected.

Completion verification originally held the global account/roster lock while
hashing the complete ciphertext. A deterministic paused-hash test blocked an
unrelated authenticated account lookup. Hashing now runs outside that lock and
outside the cache lock between bounded reads. Before the durable completion
commit, admission rechecks current session, original/current membership and the
same live upload lease. Verification evidence is bound to that lease, object,
length and immutable digest. Tests pass unrelated lookup responsiveness, real
member revocation and session invalidation during hashing, rejection of evidence
from a replaced lease, and successful retry without resetting uploaded bytes.
This corrects cache concurrency; it is not a measured Android messaging or LAN
speed improvement. Full runtime/crash tests, real TLS reconnect and the closed
shipping gate pass afterward. Windows Framework and portable server builds pass.

The bounded private attachment body is tested inside the existing signed PCGE
group envelope, with native CNG and portable AES providers. Its filename, key,
nonce prefix and plaintext hash remain encrypted to group devices. Body binding
must match the authenticated envelope's group, account, device and roster/key
epoch. Actual protected history reopen recovers the original key and exact
attachment plaintext; duplicate, truncated, oversized and substituted bodies are
rejected. A legacy text validator rejects the unsupported body before reserving
its message ID, and ordinary text still imports afterward. The outer encryption,
signature, nonce reservation, current-epoch checks and history format are unchanged.
The body is not yet dispatched by the production UI or offline cache route.

The disabled server adapter now retains the encrypted PCGE announcement after
ciphertext completion. Its canonical route and actual device signature must
match the signed descriptor's message, group, epoch, account and device. Replayed
identical publication is idempotent; conflicting or substituted envelopes fail.
Pending pages stay below the existing RPC frame bound even for maximum-size
envelopes. Every read/ACK rechecks original membership, current membership and
the device-bound session. Revoked or rejoined devices cannot fetch old envelopes.

Announcement ACKs are per device, durable and idempotent; they do not advance
file bytes or claim plaintext completion. Rollback/restart, independent receipts,
expiration and cleanup tests pass. Actual native PCGE signatures and encrypted
attachment bodies also pass managed publication/restart and native decryption
using protected recipient history. That storage/codec fixture is separate from
TLS/roster admission and is not a packaged-client delivery test.

A real pinned-TLS fixture uploads a 3-MiB object, publishes its signed envelope,
signs the source out, and downloads the complete ciphertext as another eligible
account/device. Each received block is flushed before its progress ACK; repeated
RPC reconnects preserve the lease and exact final ciphertext. No source login
is recreated. The device that was not enrolled in the group remains denied.
Physical Pi/Windows/Android performance and plaintext completion are not inferred
from that fixture.

The quota ledger now includes pending announcement and receipt metadata. Its
derived total commits in the same transaction as object/cursor/publication/ACK
mutations, avoiding a full object scan on each chunk. Startup reconciles it
against bounded object state; a missing derived ledger can be initialized for
an owned pre-release cache without changing bytes or recipients. Inconsistent
existing totals fail closed without resetting uploaded data. Actual process-kill
upload/cleanup tests pass with the ledger enabled. No physical speedup is claimed.

Production client dispatch, its protected resume adapter, local preparation and
plaintext-finalization recovery, offline old-epoch proof handling and group UI
remain gates. Current-epoch group-message checks are not relaxed to accept an
old cached announcement. Independent cache receipts are not an implementation
of the deferred Windows-first DM routing policy. Shipping transport remains closed.

A mismatched cache index previously received additive schema changes before its
instance binding was checked. A fixture reproduced that mutation. Existing index
identity is now checked before schema/PRAGMA updates; the wrong-instance fixture
is rejected with unchanged index bytes. The complete upload, announcement,
ledger, ownership, revocation and actual process-kill recovery suite passes.

### Remaining client integration sequence

The incremental `Sealer` now performs one header, 32-KiB plaintext block, or
final hash/flush step per call. It owns no socket, journal or worker. Completion
returns a single-use manifest without renaming the ciphertext, allowing protected
key persistence first. Empty, boundary and 1-MiB cases, cancellation, abandonment,
exact Unicode/content recovery, protected staging, and signed group-body tests
pass with CNG and portable group crypto. Production scheduling is not wired yet.

Prepare encrypted objects in bounded worker steps, not a whole-file blocking
operation on the profile worker. Flush complete ciphertext and durably protect
its manifest and owned sink token before the final rename. Resume the same
immutable object after interruption; never reencrypt changed input under an old
key/nonce. Cleanup must preserve partials referenced by verified staging records
and remove only authenticated abandoned temporary files.

Keep cache resume records isolated from the old transfer-journal format while
using the same TransferManager for credits, hashes, ACKs, retries and aggregate
limits. The cache route must bind the verified server, profile/device, session
and attempt generation. An old response must not mutate a newer route, and a
cache failure must not invalidate the main messaging login.

Persist the exact encrypted group envelope and its message/object binding before
publication. Retry that envelope, not a newly encrypted replacement. After a
complete ciphertext download, verify AEAD and plaintext hash before committing
the user file. Protect full-file readiness before rename so a restart can adopt
the already verified destination without overwriting it. Announcement ACK,
ciphertext durability and plaintext completion remain separate states.

Old-epoch offline announcements require verified historical roster/key evidence;
the current-epoch MessageLayer check stays fail-closed until that path is tested.
An expired/evicted object reports unavailable; reconnect must not silently renew
its lifetime. A deliberate resend creates a fresh logical attachment. Add the
group bubble/clipboard entry points only after these production paths pass.

### Earlier DM pipeline baseline

The bounded DM pipeline is implemented in the existing TransferManager, with
protected profile/per-file settings and truthful effective framing diagnostics.
Legacy 32 KiB x 1 remains the default and older/Android peers retain v1. Eight
chunk/window combinations, stale/duplicate ACKs, bounded credits, aggregate and
individual rate ceilings, restart settings snapshots, durable integrity and
legacy recovery fixtures pass. Packaged mutual-TLS transfers also pass with the
History Sync adapter initialized: generic DM packet-ID lookup now accepts v2,
while the archive wrapper deliberately remains strict v1.

Host-only measurements (single runs, 10 MiB; not physical LAN throughput):

| Requested framing | Blocking host forwarder Mbps | 20-ms-per-forward-operation fixture Mbps |
| --- | ---: | ---: |
| 32 KiB x 1 | 54.44 | 4.22 |
| 32 KiB x 4 | 74.53 | 8.24 |
| 32 KiB x 8 | 69.40 | 8.34 |
| 64 KiB x 1 | 96.91 | 5.55 |
| 64 KiB x 4 | 139.07 | 8.32 |
| 64 KiB x 8 | 136.24 | 8.32 |
| 64 KiB x 16 | 136.08 | 8.31 |

These use production packaged CoreAdapter/Node, mutual Schannel TLS, real file
I/O, hashing, flushes and protected journals, but opaque in-process forwarding
threads instead of sockets/Pi. The delay is per <=64-KiB forwarding operation,
not a fixed network RTT; stream coalescing changes how often it applies. Timing
starts after the receiver's accept call returns and ends when the observer sees
both Complete (25-ms observation interval). It excludes route and offer startup.
Process CPU was 734–1453 ms in the low-delay matrix and peak working set about
68 MB; delayed runs used 1047–1984 ms CPU and about 63 MB. These are single-process
two-endpoint fixture figures, not packaged-client CPU on two physical PCs.

Controlled default-setting ABBA checks against dev.153 measured 53.26/41.92 Mbps
before and 53.42/48.58 after. There is no demonstrated default-setting speedup
outside that variability. Optional larger negotiated frames/windows, not removal
of encryption or durability, explain the controlled high-setting result. A later
archive-initialized 64-KiB x 8 check achieved 126.91 Mbps with exact final integrity;
it is a regression check, not an additional physical performance claim.

Existing emoji, transfer statistics, media/voice lifecycle, group diagnostics,
concurrent media-plus-transfer, mailbox and archive boundary regressions passed.
The archive fixture committed 25 imports and preserved three unresolved records,
including five actual profile-worker restarts. Server portability/security tests
passed against isolated databases and TLS listeners. No real account was reset.

Cache configuration, budget/eviction plans and read-only storage binding are
groundwork, not an enabled cache. Isolated x64 and x86 tests passed 100-GiB and
8-TiB accounting without allocating those payloads, persisted config/reload,
quota reduction/leased work, reservation accounting, periodic-expiry plans,
actual Windows volume identity, absent markers, changed identity and Linux mount
table parsing. The staged admin panel preserves custom values, does not save on
open and exposes no activation or migration action. ARM32/Linux builds were
locally produced/audited; physical Linux execution of the new cache adapter is
not claimed. The read-only probe alone cannot make pathname writes race-safe.

The isolated immutable E2EE blob codec passed exact empty/Unicode-name/boundary,
1-MiB and 3-MiB round trips with actual AES-GCM, epoch/name mismatch and corruption
rejection. It is not wired to group messages or a server upload endpoint.
Durable cache reservation/lease transactions, anchored storage writes, signed
upload/download admission, actual fanout, group UI/clipboard drafts and complete
restart/migration tests remain implementation gates. No group-attachment feature
or cache-availability claim is made at this checkpoint. No Pi deployment or
migration, Android modification, identity/history/grant change, or merge occurred.

Evidence is under `build/tests/transfer-pipeline-154`, including
`archive-initialized-pipeline.log`, `legacy-final.log`, `media-current.log`,
`media-concurrent-high.log`, `archive-parser-fix.log`, `mailbox.log`,
`server-portability.log`, `cache-storage-final.log` and `blob.log`.

Independent transfer-phase checkpoint: `0607d4a`, dev.154. Its Windows client
package is `build/development/4.0.2-dev.154/PeerChat-4.0.2-dev.154-Windows.zip`,
SHA-256 `35ED0F8E04D54DF955E7C62BE7C925A16F93AE228AB30F8E707F94F007B3F6EC`.
It contains the transfer phase, not enabled server cache/group attachments.
Native dependency/HPKE containment and live C ABI load/lifetime audits passed.
The dev.153 ZIP remains byte-identical to its prior rollback hash. Physical
throughput, concurrent-media quality and the new settings UI remain unaccepted.
