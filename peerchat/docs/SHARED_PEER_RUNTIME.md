# Shared reliable peer runtime extraction

## Ownership

`shared_peer_runtime.hpp` is the per-transport synchronous execution seam used
by Windows `Node::Connection`. It consumes the existing platform TLS byte-feed
interface. It does not own sockets, a thread, a reconnect scheduler, a session
manager, a database, an identity or a delivery queue.

```
platform socket / opaque relay
    -> platform peer TLS
    -> SharedPeerRuntime
        -> existing SessionTransportManager (identity evidence / winner)
        -> existing DeliveryManager (submission / ACK / dedupe)
        -> platform/application event projection
```

Callbacks are supplied for one synchronous receive invocation; none is retained.
They execute under the caller's existing worker/gate. Stop rejects further feed
operations and prevents dispatch of already-queued frames. Destruction still
requires the platform owner to serialize admitted work before releasing TLS.

## Extracted Windows responsibilities

- bounded four-byte-length stream framing (same 256 KiB receive ceiling);
- Hello encoding, version/type/port/capability/name validation;
- authenticated TLS public-key fingerprint and expected-device comparison;
- per-transport Hello/Accepted/capability state;
- verified stored-device-pin comparison;
- authenticated friendship snapshot projection and restricted initial-pin guard;
- TLS-ready Hello emission and plaintext frame dispatch;
- Text/ACK wire parsing and unchanged delegation to DeliveryManager/Storage.

Hello names retain the Windows limit of 64 UTF-16 units, including on platforms
where `wchar_t` is 32 bits. The packet format and signature/key representation
are unchanged. A remote Accepted does not create local trust. An expected-device
mismatch remains an explicit verification event. Group-control authorization
and application/media handlers still execute in their current owners; group
membership does not become friend trust.

`SessionTransportManager` still arbitrates direct/relay winners, handles handoff
and schedules retries. `DeliveryManager` still authorizes submissions, validates
ACKs, persists history/outbox and deduplicates. Windows keeps Schannel and its
existing socket/relay adapters. Android keeps the existing opaque Keystore
identity and Bouncy Castle peer-TLS provider.

## Characterization and validation

`tests/test-node-characterization.ps1` archives committed Node source into a
unique ignored directory and compares it with current Node using the same test
fixtures/supporting sources. It never resets a worktree or rebuilds dependencies.
The fixtures now pass explicit listener ports and use a genuine historical
device pin. A mismatched key/Peer ID is separately required to fail at storage
construction instead of being treated as a persistable contact.

`-IncludeReconnect` additionally runs actual relay/direct/handoff/reconnect
coverage with the existing test-only short-backoff define. Production build
retry timing is not changed. `-ReconnectOnly` reruns that coverage alone.

`tests/test-reliable-peer-protocol.ps1` exercises 1,000 protocol/identity/lifetime
cycles, queued-callback retirement, malformed frames/Hello, exact wire bytes,
Unicode, durable history, retries/dedupe, and destination-scoped ACKs. Its byte
transport fixture does **not** prove TLS authentication; the separate actual
Schannel and opaque-signing TLS suites provide that evidence.

## Remaining integration

Extraction validation: committed/current six-peer Schannel characterization,
actual relay/direct/handoff/reconnect, 1,000 shared protocol/lifetime cycles,
profile JNI/provider lifecycle, portable Storage/DPAPI/history/outbox/ACK/dedupe,
opaque-signing TLS/Schannel interoperability and Android ARM64/release audits
pass. Full Windows product/package audit passes at **4.0.1-dev.16**. Android
debug/instrumentation/release APK builds pass. These are host/build results,
not a new physical-device claim.

## Native/JNI messaging adapter

`portable/reliable_endpoint.hpp` now binds the same protocol runtime to the
profile's existing session/delivery owners and opaque TLS provider. It owns one
concrete transport execution, not a reconnect scheduler. It advertises only
mutual TLS and logical ordering, rejects unsupported operations, and keeps
group/media handlers unavailable on the messaging-only endpoint.

`ReliableBridge` provides copied-buffer commands for authenticated friendship
snapshots, opening a peer-TLS execution, encrypted transport input/output,
restricted initial pinning, immutable event polling, durable send, paged native
history and close. All operations serialize under the existing instance gate.
Retired handles are not reused. Output chunks are at most 64 KiB; the pending
transport output ceiling remains 2 MiB. Local native history pages contain at
most eight entries. Snapshot/event records reuse the checked local serializer;
they are JNI records, not additional server/peer wire messages.

Trust-contact persistence and durable text construction are shared with Windows
Node, not reimplemented in Kotlin. GroupControl approval still does not create a
friend contact. A committed send returns its durable message ID even if the
current transport fails afterward; the outbox keeps it for reconnect. Revoked
friend authorization closes the applicable executions without deleting history.

The host endpoint proof runs 102 actual Schannel/TLS executions, then deliberately
loses an ACK, reconnects, retransmits the same message ID and verifies one history
record. Native/JNI tests exercise 1,000 invalid/unbound/stale profile lifetimes;
that rejection proof is not a positive Android profile-messaging claim. Android
debug/test/release builds, lint and audits include the new production JNI symbols.

This is still not a complete Android networked messaging endpoint. Discovery/
relay requests, manager-owned retry execution and native-backed presenter state
must be connected before requesting another phone test. The former unused
`reliable_peer_runtime.hpp` draft is not included by any production target and
is not part of this checkpoint. No Android messaging success is claimed from
host tests or earlier foundation physical tests. No ADB/device operations are
required for the current host work.

### Manager-owned relay execution requests (host green)

`portable/relay_requests.hpp` retains only pending execution records. It consumes
`SessionTransportManager::takeDueRelayAttempts` and delegates all retry delays to
that same owner. Authenticated current friendship generation, expected device,
network revision, monotonic attempt ID, single attachment and the existing
20-second peer-authentication timeout are checked before opening the TLS path.
Identical presence updates do not reset backoff. Logout/revocation closes the
applicable paths; stale attempts and old-network attachment fail closed.

JNI `presence`, `requests` and attempt-bound `open` expose these native decisions.
The Android platform must execute the returned request, not schedule its own
peer retries. The one instance mutex serializes these operations with profile,
TLS, trust and delivery state. No protocol format or Windows Schannel change.

`tests/test-relay-requests.ps1` passes 1,000 deterministic lifecycle/retry/stale/
logout cycles. `tests/test-reliable-profile-jni.ps1` is a **positive** production
JNI/Profile test using test-only host identity/Windows SQLite/DPAPI primitives
and the production pinned Java TLS provider: eight reopen/Hello/trust/text/ACK
cycles, including discard-ACK/profile-destroy/reopen/same-ID retry and one durable
recipient history record. This does not claim Android Keystore execution, cold
OS process persistence, live server relay, or physical Android messaging.

All Android variants/lint and ARM64/release dependency audits pass. Windows
identity/DPAPI/storage/history/outbox/ACK/dedupe and pinned TLS/Schannel regressions
pass. Full Windows build/package audit: `4.0.1-dev.19`. The previously noted
load-sensitive reconnect wall-clock assertion was rerun unchanged and passed
after simultaneous compiler jobs ended; it has not been weakened.

### Physical relay adapter prepared (not device-validated)

`ServerAccounts.openRelay` executes the existing authenticated `relay_attach`
request over the already-validated pinned outer server TLS. It checks the
friend-only response scope and unchanged session binding before exposing opaque
bytes. Pairing reads are bounded; idle connected reads remain interruptible by
closing the socket. `AndroidRelayExecution` executes exactly one native attempt,
feeds/takes copied peer TLS bytes, and retires it through the native owner. It
contains no peer reconnect scheduler or Hello/trust/delivery implementation.

`RelayFrames` validates the existing 43-byte ASCII session prefix and exact
64 KiB payload bound; 1,000 host copied-buffer/session-binding cases pass,
including malformed, empty, oversized and changed-session envelopes. All Android
variants/lint and full Windows build/package audit pass (`4.0.1-dev.20`). These
component tests are not an actual Android/Windows message round-trip claim.
Application lifecycle, authoritative friend metadata projection and presenter
binding remain to be connected and tested before another phone request.
