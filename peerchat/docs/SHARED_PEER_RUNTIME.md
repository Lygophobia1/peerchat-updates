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

This extraction is not yet a complete Android messaging endpoint. JNI must bind
the same runtime to production peer sessions and existing owners, then connect
discovery/relay outputs and native-backed presenter state. The former unused
`reliable_peer_runtime.hpp` draft is not included by any production target and
is not part of this checkpoint. No Android messaging success is claimed from
host tests or earlier foundation physical tests. No ADB/device operations are
required for the current host work.
