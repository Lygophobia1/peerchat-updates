# Bounded text-history foundation — dev.47

This checkpoint adds shared native paging/import machinery, not finished
user-facing archive sync. Explicit enrollment UI, platform metadata/route wiring,
provenance presentation and attachment fetch still need integration. Ordinary
login or account membership does not enable archive access.

## Owners and protocol

SessionTransportManager still owns archive-scoped peer TLS. Its existing shared
ReliableEndpoint accepts only the exact session binding, then canonical
ArchiveHistory/v1 request (kind 2) or page (kind 3) controls. The caller supplies a
worker-confined handler; no socket, thread, retry timer or session owner is added.
The server sees only encrypted TLS and signed public grant metadata.

HistoryReader borrows the existing Profile, Authorization, Storage and
DeliveryManager. It has one outstanding request, bounded progress and cancellation.
Source pages contain at most eight records and fit the existing 70,000-byte frame
limit. Maximum-sized historical text remains supported; pages shrink by bytes.
Authorization is rechecked before release and before every durable import.

Each page preserves original attribution plus a separately signed archive
attestation. Legacy content is ArchiveAttested, never upgraded to OriginalSigned.
The receiver authenticates the entire page before importing its first record.
Record acceptance still belongs to DeliveryManager and the existing messages
table. There is no archive content database or server plaintext archive.

## Cursor and restart semantics

An additive identifier-only message_archive_index assigns AUTOINCREMENT positions.
Existing history is backfilled in existing row order. Message IDs, timestamps,
Lamport ordering and ordinary UI history cursors are unchanged. Deleted positions
are never reused. A protected random store identifier binds a cursor to one source
database; it is not a key or identity.

Each catch-up window seals an inclusive high watermark. Later live messages are
excluded until the next window. Pages advance by archive position, including gaps
left by deletion. A protected CAS checkpoint binds account, reader, approved
issuer, source and conversation; it stores source identity/window/cursor/progress.
It advances only after all records are durable. Crash after individual imports
but before the checkpoint safely replays/dedupes that page. Reconnect may reissue
the same cursor with a new request ID; late responses cannot advance it.

Archive imports retain original sender/recipient metadata and protected provenance.
They create neither send-outbox entries nor fabricated recipient ACKs. Existing
real delivery/outbox state wins. Authenticated live/mailbox races can reconcile a
same-account sibling recipient only against the previously verified protected
archive proof and matching logical ID/body/time/order. Conflicting IDs fail closed.

## Automated gates

tests/test-archive-security.ps1 -HistoryStorage -PeerTls -Provenance exercises:

- nonlexical IDs and identical timestamps; sealed windows with concurrent live data;
- 67 durable imports, bounded pages, disconnect/reissue and restart halfway;
- crash after a record but before checkpoint; duplicate and live/history convergence;
- whole-page signature validation, wrong source-store identity and malformed frames;
- cancellation, stale policy writer, revocation and no outbox/fake ACK;
- real mutual Schannel archive TLS: 25 records over four pages without friendship,
  contact pins or ordinary DM permission.

These are automated native tests, not physical Windows/Android archive acceptance.

Dev.47 validation passed: full Windows build, runtime imports/restricted loading,
94 Android JVM tests (zero failures/errors), debug/test/release builds and lint,
JNI/ARM64/dependency/release containment audits, strict ARM64 shared-header syntax,
Storage/identity/DPAPI/profile/history/outbox/ACK/dedupe/provider regressions,
102 existing mutual Schannel endpoint lifecycles and 1000 shared protocol cycles.
Mailbox tests include sender disconnect, protected persistence/restart and signed
receipts; TransferManager and durable process-restart transfer tests also passed.
The history fixture additionally checks old-schema index backfill without changing
outbox state and maximum-sized Unicode records split by the wire byte budget.

Development updater dev.46 -> dev.47 safe extraction/version/runtime-hash audit
passed. The package SHA-256 is
49c07ee64ec9d78f14cffc2975ec0a428d9e79629e93d49d95b38c2f365d09c6.
APKs are preserved under build/android/4.0.2-dev.47. No phone installation,
publication or branch merge was performed.

## Preserved server deployment

The client-only dev.47 work does not change or redeploy the Pi server. The latest
validated server is dev.46, checkpoint c1acf13, started as PID 23441 on 7443:
/opt/peerchat-test/server/4.0.2-dev.46/PeerChat.Server, using
/opt/peerchat-test/config/4.0.2-dev.46/server-config.json and the existing
/opt/peerchat-test/data/4.0.2-dev.33/physical-client.db.

At promotion, SQLite integrity and account/device/friendship/group/mailbox/push
counts matched. The existing certificate pin remained
62A181282FE940F03F6A35541220A05F9ED2CB85A41F6DE3B0DF111286BA17E3.
TURN/FCM configuration and coturn PID 2718 were preserved. This is deployment
evidence, not a claim of new physical TURN/FCM/archive tests.
