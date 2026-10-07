> HISTORICAL ONLY — describes the dev.17 evaluation/proof, not the current provider or build instructions. The former provider, evaluation switch and dependency have been removed. See GO_HPKE_PRODUCTION_INTEGRATION.md for the current Go integration.

# Encrypted mailbox evaluation runtime

Status: **TECHNICALLY APPROVED provider / SHIPPING LICENSE UNRESOLVED**.
This is not production distribution approval or full mailbox acceptance.

## Pinned provider and containment

Windows evaluation uses unmodified wolfCrypt 5.9.4 (`v5.9.4-stable`),
commit `3c5eead44904df64e6a5a1f4ebdce377d35a849a`.
Source archive SHA-256:
`C20D50CE143CBF2E8DBBD91E1035C75E99112DB7774D399E969894BE036367A7`.
MinGW static artifact SHA-256:
`830B85BE3482A663F8F76A167268F497388A6B016F224F40D10AE80733D8E097`.
Pinned cached sources/artifacts are reused, not downloaded or rebuilt normally.
Crypto-only/static release build enables HPKE, crypto callbacks, HKDF, AES-GCM;
assembly is disabled. Compile definitions include `WOLFCRYPT_ONLY`, `NO_CERTS`.

Explicit evaluation command: `./build.ps1 -All -HpkeEvaluation`.
Outputs live under `build/evaluation/<immutable revision>`, not development.
The package includes `HPKE-EVALUATION.json` and the upstream GPLv3 license.
An embedded Core marker also identifies evaluation binaries if the notice is
removed. Normal updater directory and ZIP packaging reject either marker.
No publication, license purchase, or vendor contact is authorized here.
The normal provider factory fails closed with no shipping HPKE provider.
Upstream library internals are hidden from the Core DLL export table.

Future distribution choices remain GPLv3 distribution, a commercial wolfCrypt
license, or a separately approved replacement for `HpkeProvider`. This document
does not decide their legal suitability.

## Cryptographic/ownership boundaries

`HpkeProvider` is a library-independent one-shot interface. The wire uses RFC9180
Base suite IDs DHKEM(P-256, HKDF-SHA256), HKDF-SHA256, AES-128-GCM. Existing
device signing/TLS identity and its P1363 signature representation are unchanged.
Recipient keys are separate agreement-only non-exportable CNG/Keystore handles.
No private scalar is an interface argument or persisted registry field.

WolfCrypt's process-global callback registration is serialized across *all*
provider instances. Fresh contexts/RNG/ephemeral keys and callback registrations
are operation-local and RAII cleaned. Each callback verifies the exact public-only
recipient object and validates the ephemeral curve point before opaque DH.
Failure never falls back to a software private key. Concurrent requests are
safe; cryptographic execution is deliberately serialized, not claimed parallel.
Cancellation/shutdown is cooperative at operation boundaries; it does not
unsafely interrupt an OS cryptographic call. Transient DH is erased.

KeyManager operation leases serialize revocation against decryption/acceptance.
The protected SQL key registry MUST use a distinct database file from message
Storage: its write lease may span durable receive, so using two connections to
the same file would cause self-contention. Registry rollback, tamper, missing
initialized keys and provider errors fail closed. Explicit rotation preserves
provisioning intent and retired history keys; it never regenerates a missing
initialized key silently. Retained metadata is bounded to 32 generations.

## Protocol and persistence implemented so far

Canonical signed recipient registrations bind account/device, suite, key ID,
generation, predecessor, public point, timestamp and recipient storage consent.
Signed envelopes bind both account/device pairs, key generation/ID, logical
message ID/order, protocol version and bounded creation/expiry through AEAD AAD.
The private text body preserves original message time and content. Device
signatures preserve sender attribution independently of HPKE Base mode.
Recipient ACK signatures bind account/device, sender device, message ID and
SHA-256 of the exact submitted envelope. The server cannot fabricate these ACKs.

Native DeliveryManager primitives persist ciphertext before submission, reuse
the exact envelope/ID on retry, retain outbox state at `Stored`, validate/decrypt
before normal durable native receive/dedupe, protect pending receipts and advance
to `Delivered` only after verified recipient proof. Existing numeric states
Queued=0/Sent=1/Delivered=2 remain unchanged; Stored=3 uses explicit advancement
rank so numeric ordering cannot prevent the real ACK transition.

The server application persistence layer stores only opaque envelopes, public
registrations/proofs, routing IDs, expiry and signed receipts. Signed registration
requires an already authorized signing device; a bearer session alone cannot
substitute a key. Store/fetch recheck friendship, blocks and device ownership.
Rotation admits new submissions only to the active generation. Previously stored
retired-key envelopes may be fetched until expiry. Signed revocation rejects
future submissions and deletes pending ciphertext for that key. Device removal
immediately disallows store/fetch; lazy purge/startup removes orphaned rows.
TTL is at most seven days. Current bounds are 256 pending envelopes / 8 MiB per
recipient and 4096 rows / 64 MiB globally. A signed ACK clears payload and retains
bounded proof/tombstone for sender reconciliation. Receipt pagination is explicit.
No server retry thread, socket owner or new session manager is introduced.

This is not a forward-secrecy claim: retained static recipient keys decrypt
their previously addressed envelopes until explicit retirement/revocation policy.

## Automated evidence and pending integration

Passed: RFC vectors; bidirectional stock BC and patched host-oracle tests;
1000 CNG round trips across two keys/eight concurrent provider clients; bounded
handles, repeated initialization, cancellation and shutdown during DH;
on-curve/no-fallback/tamper rejection; 100 real registry/CNG reopen cycles;
native signed binding/envelope/receipt strict parsing; blind SQLite server restart
and idempotent ACK; native Stored/Delivered and live/mailbox ACK races;
Storage/DPAPI/history/outbox/ACK/dedupe, authenticated group fanout and remembered
authentication regressions. Evaluation Windows full build/import audit passed at
4.0.2-dev.11. Android debug/test/release/host tests and lint also passed before the
latest adapter additions; rerun results must accompany their checkpoint.

Still pending: complete application servicing/Windows UI and Android FCM fetch
binding, multi-device logical-delivery integration, full quota/rotation/restart
integration coverage, and physical Motorola Keystore interoperability plus
foreground/background/locked delivery with sender fully closed after Stored.
Do not call a host oracle test physical Android acceptance. No phone availability
check is a reason to stop host-side integration work.

Execution/security regression evidence is not independent cryptographic
certification. Shipping license and technical end-to-end acceptance remain
separate gates.
