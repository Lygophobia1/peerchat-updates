# Stage 1B: encrypted group-message envelope

This is a client cryptographic/message layer, not a transport. Stage 1C now wires
online ciphertext fanout and UI send/receive through the existing authenticated
server event channel. See `GROUP_CIPHERTEXT_FANOUT.md`. Group media is not
implemented. No signed-plaintext or GroupControl chat fallback is permitted.

## Owners and authorization

`group_messages::MessageLayer` borrows the existing profile-local `Storage`,
persistent device `Profile`, authenticated `GroupKeyManager`, and signed
`GroupState`. It creates/receives envelopes without sockets, reconnect scheduling,
new session ownership, or Friend-trust promotion. Stage 1A remains responsible
for authenticated server roster input and peer-TLS key delivery.

Each operation rechecks the current roster and exact local account/device tuple.
The authenticated roster must match the signed membership/public-key ledger.
The wire group ID is the server-authoritative roster ID, not a route or the
local ledger ID. Both roster and key generation must equal the current roster
generation. Stale and future generations fail closed, even if an old key remains
locally available. The sending account must be a current, non-invited member and
the sending device must match its roster binding and signed public key.

## Canonical wire format v1

`PCGE` is deliberately separate from the old `PT1` signed group-event encoding.
Integers are unsigned, fixed-width, big-endian. IDs are fixed-width lowercase
ASCII hex. There are no optional fields or alternate serializations.

| Offset | Bytes | Field |
| --- | ---: | --- |
| 0 | 4 | `PCGE` domain/magic |
| 4 | 2 | Envelope version = 1 |
| 6 | 32 | Server group ID |
| 38 | 8 | Roster generation |
| 46 | 8 | Key generation |
| 54 | 32 | Random message ID |
| 86 | 8 | Sender account ID |
| 94 | 64 | Sender device Peer ID |
| 158 | 12 | Nonce |
| 170 | 4 | Ciphertext byte length |
| 174 | N | Ciphertext (0..65,536 bytes) |
| 174+N | 16 | GCM authentication tag |
| 190+N | 64 | Raw P-256 device signature (`r || s`) |

Maximum total length: 65,790 bytes. Parse bounds and exact total length are
checked before any variable-length allocation. Zero/overflowing identifiers,
unsupported versions, malformed IDs, truncation and trailing bytes are rejected.
The body is bounded private application bytes; UI text encoding/ordering and
fanout delivery integration are Stage 1C concerns. No timestamps are required
to authenticate or dedupe this layer.

## Cryptography

- **AEAD:** AES-256-GCM, using maintained Windows CNG (`bcrypt.dll`), already
  used by PeerChat for device identity/hash/randomness. No new crypto dependency,
  custom cipher, MAC, wrapping scheme, or secret server input.
- **AAD:** all 174 canonical header bytes, including version/domain, group ID,
  both generations, message ID, account/device identity, nonce and ciphertext length.
- **Attribution:** SHA-256 followed by ECDSA P-256, using the same persistent
  private device key as the signed membership ledger. Sign the canonical header,
  ciphertext and GCM tag. Verify with the authorized member's existing public
  key, never with a public key supplied by the envelope. A shared group key does
  not authorize impersonating another member.
- **Nonce:** 96 CNG-random bits per encryption, reserved transactionally *before*
  use in the sender's protected profile database, scoped by the actual key's
  SHA-256 fingerprint. A local collision is retried, a restart cannot reuse a
  reserved nonce, and failed encryption burns its reservation. A conservative
  per-device cap of 2^24 encryptions per key fails closed. As with any random
  nonce scheme, independent devices rely on CSPRNG collision resistance, not a
  distributed nonce coordinator. Profile database rollback/clone detection is
  not implemented; do not restore a live key/profile backup and resume encrypting
  concurrently as the same device.

Keys and temporary decrypted buffers are wiped on exit. Provider failures emit
only a bounded rejection code; this layer does not log provider inputs, PCM,
plaintext, content keys or private device keys.

## Receive order and durable records

1. Bounded parse and supported format.
2. Current local group/roster/ledger and local membership binding.
3. Expected server group and current roster/key generation.
4. Sender membership and device/public-key binding.
5. Device signature.
6. Current local content key and AES-GCM authentication/decryption.
7. Atomic profile-local message-ID dedupe and protected history commit.
8. Only then return plaintext to the caller.

Identical `(server group ID, message ID)` replay returns `duplicate_message`
without plaintext or another history insertion. Authenticated conflicting ID
reuse returns `conflicting_message` without overwriting the original. Failed
authentication does not consume a message ID. SQLite receives only DPAPI-wrapped
envelope/body records through the existing `objects`/transaction implementation;
there is no second outbox or delivery scheduler. Local creation also persists the
exact ciphertext bytes and authenticated local body for future retries.

Previously authenticated local history can be read separately after rotation.
That history read is not a network receive operation and does not relax current
membership or generation checks. No new member receives historical keys.

## Validation

Run `tests/test-group-message-envelope.ps1 -IncludeStage1A` as the normal Windows
user (DPAPI under the sandbox-only user is not an application acceptance gate).
It uses unique profile directories and restricted runtime PATH. Coverage includes
empty/minimal/normal/maximum bodies, canonical round trips, tampered metadata,
signatures/ciphertext/tag, re-signed AAD tampering, missing/wrong keys, unauthorized
accounts/devices, stale/future generations, parser bounds, durable replay and
nonce reservation across restart, member removal/current-key rotation, protected
local history, and absence of plaintext in serialized server-facing bytes and
SQLite files. Existing scope, actual peer-TLS handoff and three-member rotation
regressions prove the Stage 1A boundary remains intact.

Physical group delivery cannot be claimed from this checkpoint: Stage 1C still
needs to integrate opaque fanout with the existing reliable delivery layer.
