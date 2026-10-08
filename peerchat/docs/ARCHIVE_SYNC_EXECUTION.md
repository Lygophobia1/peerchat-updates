# Archive sync execution — integration in progress

This document records the bounded native integration layer following dev.53.
It is not a declaration of physical-acceptance readiness.

Dev.54 is retained as the first validation build. Dev.55 includes the subsequent
generation-safe cancellation and removal of revoked scheduled archive demand.
Neither build is the final end-to-end physical-acceptance candidate.

## Existing authorities

- SessionTransportManager owns route demand, backoff, pending attempts, winners,
  stale transports and retirement. Archive demand uses the same candidate plans,
  with a separate archive continuity key. Leases are bounded to 60 seconds and
  restricted to explicitly admitted source/reader pairs. No new scheduler exists.
- DeliveryManager remains the only import/dedupe/history authority. Archive imports
  do not create an outbox entry, recipient ACK or Delivered claim.
- Storage retains encrypted history, provenance and protected CAS checkpoints.
- TransferManager already supports durable sender/receiver process-restart recovery;
  archive attachment integration must reuse it rather than implement another engine.

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

## Remaining before a physical candidate

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

Still required:

- Integrate authenticated attachment catalogs and TransferManager archive transfers.
- Complete mobile cache/application projection and candidate acceptance audits.
- Produce final versioned clients and the requested physical checklist.

The Pi deployment has not been modified by this layer. Observed running state:
dev.46, PID 23441, TCP 7443, `/opt/peerchat-test/server/4.0.2-dev.46`, configuration
`/opt/peerchat-test/config/4.0.2-dev.46/server-config.json`, database
`/opt/peerchat-test/data/4.0.2-dev.33/physical-client.db`. Recheck before any deployment.
