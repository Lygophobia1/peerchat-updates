# Archive security foundation

This is shared-native groundwork, **not an enabled history-sync feature**.
No client UI, server registration, archive route, history importer, or media
archive fetch is enabled by these headers. Existing messaging is unchanged.

## Authorization

`archive_authorization.hpp` uses the existing device signing provider and public
identity representation. There is no new identity or private-key export.
The canonical `PeerChat/ArchiveAuthorization/v1` grant binds account, issuer,
subject public key/Peer ID, read/serve capabilities, monotonically increasing
generation, issuance ID, issuance time, and revocation. Public account/device
membership is necessary but explicitly insufficient.

Both local authority approval and grant issuance require explicit consent. An
ordinary account login, device list, friendship, or incoming grant must not call
authority approval implicitly. A locally disabled authority cannot silently be
re-enabled by an old grant. Re-enrollment of a subject uses a higher generation.

Protected local policy uses the existing Storage objects/protection provider and
a transactionally checked expected-value write. Concurrent stale instances fail
closed. Signer and persistence failures do not publish partial grants.

An injected current authorization provider must confirm the exact generation,
capability, issuance ID, and current device membership on every permission check.
Missing/stale metadata denies. A cached signed grant is not freshness evidence.
**That production metadata provider is not implemented yet.** Tests inject a
controlled authoritative readout; this must not be mistaken for server wiring.

## Provenance

`archive_provenance.hpp` keeps original device attribution, message ID, account
context, timestamp, logical ordering and text separate from archive assertions.
The source can attest only an exactly matching locally stored record, with
account context checked against accepted device-account bindings.

The existing SHA-256/device signature implementation authenticates a distinct
`PeerChat/ArchiveStoredRecordAttestation/v1` domain. The assertion binds the
canonical record digest, archive identity, account, authorizing issuer, grant
generation, and archive timestamp. Transport framing is bounded by the existing
70,000-byte ceiling; strict parsers reject unknown versions, truncation, trailing
bytes, malformed lengths and mismatched digests.

Signature validation happens in `ProvenanceVerifier::verify`, after structural
decoding and before any future importer may commit a record. Parsing alone is
not authentication. Delivery state and local row cursors are not accepted as
portable proof. There is deliberately no import/ACK/outbox mutation here.

Legacy unsigned DMs return ArchiveAttested, never OriginalSigned. A locally
originated label requires a matching existing local record, not a remote claim.
Revocation prevents future access; it cannot erase legitimately received copies.

## Remaining integration

1. Authenticated bounded server grant metadata and current-generation readout.
2. Explicit trusted-device selection/confirmation, enrollment and revocation UI.
3. Archive-purpose relay/TLS permission through existing SessionTransportManager,
   with no friend privilege, second session owner, or retry scheduler.
4. DeliveryManager/Storage import and provenance persistence, stable bounded
   paging/checkpoints, live/mailbox convergence, cancellation and progress.
5. Existing TransferManager-based attachment fetch and bounded phone media cache.

No server source/configuration/deployment change is part of this foundation.
Physical archive/history acceptance remains pending.

## Focused validation

Run `tests/test-archive-security.ps1` and the same script with `-Provenance`.
Fixtures use isolated generated test profiles, never normal device identities.
Coverage includes explicit consent, opaque signing/provider failure, same-account
denial, account/device/capability/signature tamper, protected reopen, revocation,
stale metadata/generation, re-enrollment, device removal and stale policy writers;
plus canonical provenance round-trip, large Unicode, malformed envelopes,
unresolved/wrong account context, substituted records and honest legacy labels.

Foundation validation at 4.0.2-dev.44: both focused tests PASS; complete portable
Storage host regressions PASS (Windows DPAPI, identity/opaque signing, history,
outbox, ACK/dedupe, SQL/protected-record reopen/tamper, key registry and group
envelope); Android ARM64 header syntax check PASS; Android JVM/debug/test/release/
lint PASS; JNI/ARM64/dependency/TLS artifact audits PASS; full Windows build and
runtime import/package audit PASS; version/updater extraction/hash audit PASS;
diff whitespace check PASS. These are not physical archive-sync acceptance.
