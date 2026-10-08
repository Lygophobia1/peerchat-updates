# Archive sync execution — dev.59 physical candidate

This document records the bounded native integration layer following dev.53.
Dev.59 is the combined physical-test candidate. Automated evidence below is not
a claim that physical archive or attachment acceptance has passed.

Dev.54 is retained as the first validation build. Dev.55 includes the subsequent
generation-safe cancellation and removal of revoked scheduled archive demand.
Neither build is the final end-to-end physical-acceptance candidate.

## Existing authorities

- SessionTransportManager owns route demand, backoff, pending attempts, winners,
  stale transports and retirement. Archive demand uses the same candidate plans,
  with a separate archive continuity key. Execution renews bounded 30-second leases,
  restricted to explicitly admitted source/reader pairs. No new scheduler exists.
- DeliveryManager remains the only import/dedupe/history authority. Archive imports
  do not create an outbox entry, recipient ACK or Delivered claim.
- Storage retains encrypted history, provenance and protected CAS checkpoints.
- One TransferManager per profile supplies live and archive bytes, chunks, hashing,
  resume, cancellation, partials and atomic completion. Archive code borrows it.

## Conversation discovery and sync

ArchiveHistory/v1 control kinds 4 and 5 are bounded manifest request/page messages
inside admitted archive peer TLS. They carry a correlation ID, source store ID,
stable archive-position window and at most eight conversation entries. They never
travel through server-readable history payloads. Existing kinds 2/3 carry text pages.

Storage queries conversation first positions inside a sealed archive watermark.
Message IDs, timestamps and filename ordering are not cursors. Manifest and text
pages are validated canonically; malformed/duplicate/nonascending entries fail
closed. Each text page still passes full provenance verification before import.

SyncReader borrows existing profile, Storage, authorization and DeliveryManager.
It owns one application job, not a route, socket, thread or retry timer. One pending
request provides backpressure. It processes one conversation at a time and saves
manifest progress only after that conversation is durable. A process restart
re-discovers the unfinished conversation and resumes its existing HistoryReader
checkpoint. Replay dedupes through DeliveryManager. Completion does not trigger
another scan on a transport poll; a new scan is explicit. Cancellation/History OFF
preserve history, grants and resume cursors.

## Historical account evidence

Authenticated friend snapshots and verified authenticated archive descriptors retain
device/account bindings in the existing protected Storage object backend. Exact
retries are idempotent, contradictory ownership is rejected and corrupt protected
records are never silently overwritten. These bindings are historical evidence,
not contacts, pins, friendship, group membership or permission to start a DM route.

Unknown historical device/account evidence still fails closed. An archive-supplied
username, account claim or conversation label cannot manufacture sender identity.

## Application integration

Dev.56 wires the existing Windows relay adapter and Android relay execution to the
native archive authorization value. Requests, authenticated server hints and relay
responses carry the exact account/issuer/source/reader/grant-generation binding.
Both TLS implementations require the corresponding device key and exact in-TLS
scope binding; neither path pins a friend or drains a DM outbox.

The native Execution component borrows the existing profile owners. Sync/Pause
controls and bounded progress are available in both Settings surfaces. A protected
explicit job intent survives owner/process restart; one authenticated startup
metadata check can resume it. Metadata is refreshed only during active reading or
bounded serving work. OFF pauses admission without deleting intent, history or
cursors. Completion/pause/revocation retires manager-owned demand. A 15-second
page deadline resets the pending page and uses the existing STM backoff. Invalid
archive data/storage stops archive work rather than the ordinary messaging worker.

Dev.56 automated evidence: full immutable Windows build/import audits; Android
debug/test/release/lint and 105 JVM tests (zero failures/errors); real paired Node
workers with zero friends, opaque relay and mutual Schannel TLS; native 25-record
history over real TLS; durable intent/OFF/ON/restart/deadline checks; 1,000 relay
lifecycles plus zero-friend archive retry/revocation. No physical success is claimed.

## Attachment integration

ArchiveAttachments/v1 carries bounded catalogs (eight records/page, 40 KiB limit),
explicit fetch requests and wrappers around existing FileTransfer frames inside
the same authorized peer TLS. Original logical attachment IDs, authenticated
account/device attribution, immutable name/size/SHA-256 metadata and archive
attestation survive import. Attestation is not an original-sender signature.
Local paths and resume tokens never travel in catalog/control messages.

Source files are verified against the catalog digest before an offer. The existing
TransferManager owns hash verification, durable partials, reconnect and final ACK.
Protected transfer bindings constrain each request to its current grant and source.
Revocation/OFF stops future bytes without erasing committed history. Lost completion
ACKs are answered only from an exact protected completed binding. Missing sources
become bounded failures, not an endless request loop. Duplicate requests coalesce;
already-possessed logical attachments are not downloaded again.

Windows Settings exposes Historical attachments, Refresh attachments, bounded
Next attachment page and Download selected attachment. Downloads use the existing
File transfers progress/cancel window. Files receive unique ID prefixes to prevent
overwriting duplicate Unicode names. A source must still possess its unchanged
completed file: this checkpoint does not silently duplicate every file into an
archive or evict the only copy. Dedupe is by logical attachment ID, not a global
content-addressable store across unrelated logical IDs.

Android shared/native code uses the same catalog/TransferManager with a protected
Storage journal and app-private no-backup file provider. O_NOFOLLOW/inode checks,
fsync before durable progress, no-overwrite commit and bounded conservative quota
protect restart handling. No second Kotlin transfer engine exists. Native catalog,
download and cancel operations are present, but Android attachment download UI is
deferred; Android text synchronization is available in Settings now. Android file
provider instrumentation builds but has not run on the unavailable physical phone.

## Dev.59 automated evidence and remaining physical gates

Full Windows build/package/import/version and Development updater extraction/hash
audits pass. Android debug/test/release/lint and ARM64/JNI/dependency audits pass.
The JVM result is 105 tests, zero failures and zero errors.
The updater is unsigned Development-only; nothing is published.

Focused catalog tests cover tamper/scope/revocation, CAS/reopen, Unicode/duplicate
names, verified bytes, pending-before-offer restart, lost completion ACK, cancellation,
missing sources and protected-journal corruption. Existing TransferManager tests
cover real sender/receiver process restart and hash/partial/error paths. A packaged
paired-Node harness with zero friends establishes real mutual Schannel TLS over
opaque relay, synchronizes the catalog and downloads a verified 100,000-byte Unicode
attachment through one TransferManager. The harness explicitly propagates EOF on
route retirement, matching production RelayTransport; it does not retain a stale
opposite byte pipe. Native archive tests exercise real TLS and bounded text pages.

See ARCHIVE_PHYSICAL_ACCEPTANCE_DEV59.md for physical A–M checks. None is claimed
passed by the host tests. No ADB install or physical Keystore execution occurred.

Final regression commands also passed: test-network-presence.ps1 (1,000 presence
cycles, offline composer and 100 lifecycle drains), test-remembered-login.ps1
(32 authenticated resumes), test-mailbox-protocol.ps1 (sender-offline fetch/ACK,
durable receipt, device/pin rejection), test-node-characterization.ps1
-ReconnectOnly, test-voice-settings.ps1, test-transfer-ui.ps1 and
test-transfer-session.ps1 (real peer TLS, reconnect/profile reopen, Unicode binary
and coexistence with message ACKs). Two explicit C# test compiler lists needed
ArchiveEnrollmentExecution.cs because DiscoveryClient now references ArchiveRelayScope;
only their dependencies changed, not their assertions or production behavior.

Preserved Android APK SHA-256 values under build/android/4.0.2-dev.59:

- debug: 0043037742F0C12916D3B3CE323E6ED88C8EF2F202E614E2735F09B0E46A23CF
- test: A69D72E88D87BA4BEDD5F80979A7F9F799ED19876B31333758FDEA9F286465D0
- unsigned release: 43408AC74BF09341F59B9A39E9EED1AEBA22DFE73B39EACCDBAD0FB13CAFA62B

The copied versioned APKs were compared with their build outputs. Windows and
Android binaries share dev.59 source metadata; subsequent changes before the
checkpoint were test/compiler-list/checklist documentation only. Binary archives,
object files and caches are not source checkpoint contents.

The Pi deployment has not been modified by this layer. Observed running state:
dev.46, PID 23441, TCP 7443, `/opt/peerchat-test/server/4.0.2-dev.46`, configuration
`/opt/peerchat-test/config/4.0.2-dev.46/server-config.json`, database
`/opt/peerchat-test/data/4.0.2-dev.33/physical-client.db`. Recheck before any deployment.
