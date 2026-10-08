# Archive authorization metadata — dev.45

This checkpoint adds the Linux server metadata boundary. It does **not** enable
automatic history access, archive enrollment UI, or history transfer.

## Public device descriptors

An existing registered device publishes its existing signing public key using
the signed canonical PeerChat/ArchiveDeviceDescriptor/v1 domain. The server
checks authenticated account/device ownership, canonical P-256 key/Peer ID and
signature. One immutable descriptor per identity is retained. The directory is
bounded to 32 devices/account. It is a selector/input for an explicit future
trust decision, NOT implicit archive permission.

## Grants and revocation

The server verifies the shared-native PeerChat/ArchiveAuthorization/v1 bytes
against the issuer descriptor and authenticated issuer/account. The target must
have a verified descriptor and belong to the same account for a new grant.
ReadHistory and ServeHistory remain distinct capabilities.

The existing AccountDatabase owns two additive SQLite tables. No new database,
socket/session owner, background worker or crypto primitive is introduced.
Only public descriptors and signed grants are stored, never history or keys.
Admission is transactionally serialized; first generation is 1, subsequent
generations must be exactly previous+1. Exact latest retry is idempotent;
conflicting/stale generations fail. Revocation tombstones survive restart.

A device-removal trigger disables affected grants without changing signed proof
bytes. Re-registering the same Peer ID cannot revive them. Only a newer valid
signed enrollment can restore eligibility. Removed issuers/subjects fail current
authorization. Account deletion cascades only the account's metadata.

## Authenticated RPCs

- archive_descriptor_register: signed public descriptor for the authenticated device.
- archive_grant_register: signed grant/revocation from the authenticated issuer.
- archive_authorization_state: bounded public directory/grants and current
  eligibility for the authenticated account, plus the existing server epoch.

Ordinary account access may read public metadata; it cannot forge known device
signatures or override endpoint approval. Clients MUST independently pin an
explicitly approved authorizer, verify each proof and current generation, and
treat currentlyEligible=false/missing/stale server state as denied. Server
admission is not a substitute for native authorization or explicit consent.

The next session integration must recheck current authorization before each
bounded page, invalidate stale open routes, and never grant DM/friend rights.
No archive-purpose relay admission is enabled in this checkpoint.

## Validation

- Native/managed canonical descriptor/grant interoperability and SQLite restart,
  signature/account/device/capability/tamper, generations, revocation, device
  removal and signed re-enrollment: PASS.
- Actual ARM32 Pi isolated TLS/RPC: PASS, including authentication, real native
  signatures, conflict/replay denial, revocation/re-enrollment, process restart
  and deletion/re-registration without revival. Test port 17443; live data not used.
- Pi lifecycle: three SIGINT/SIGTERM start/drain/close cycles, stable account/DB,
  integrity_check and unique server epochs: PASS.
- Presence/auth/Remember Me/FCM host regressions: PASS.
- Mailbox canonical HPKE/store/fetch/signed ACK, sender-independent persistence,
  quotas, expiry, revocation, dedupe and restart: PASS.
- Group creation/roster/fanout regressions: PASS.
- Windows full build and runtime package/import audits: PASS.
- Android: 94 JVM tests, zero failures/errors; debug/test/release/lint and
  ARM64/JNI/pinned-TLS/dependency/credential-containment audits: PASS.
- Linux x64/ARM64/ARM32 version/runtime-only packages: PASS.

The first isolated RPC test needed a harness correction: missing authentication
is allowed to fail closed by closing TLS; valid authenticated operations still
require a complete success response. No production trust handling was weakened.

No physical archive/history sync success is claimed. Explicit enrollment UI,
archive-purpose peer TLS, durable text import/paging and attachment fetch remain
integration work. Prior accepted voice, screen/file-transfer and mailbox paths
are unchanged. No normal Android identity/data was touched.
