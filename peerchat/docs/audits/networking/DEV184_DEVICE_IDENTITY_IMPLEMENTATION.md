# dev.184 identity discovery implementation checkpoints

The scoped enrollment-authority extension was approved after `aedddcb`.
This document tracks implementation, not a release/physical acceptance claim.
No Pi deployment is authorized for this milestone.

## Foundation (not yet connected to production clients)

- `device_identity_endorsement.hpp`: canonical, bounded, domain-separated public
  target possession and one-hop authority endorsement records. Existing P-256
  identity providers sign original messages. Scoped signing checks explicit
  captured approval, account/server, exact actor/target keys, authorization and
  local session generations, expected revision, challenge/session binding, time
  bounds, and replay. It exposes no JNI general-purpose signing interface.
- `device_identity_directory.hpp`: profile/server/account-bound encrypted record
  using existing protection and atomic-store abstractions. Explicit authority
  fingerprint confirmation is separate from directory reconciliation. Root,
  proofs, generation/revision watermarks and tombstones commit together. Missing
  roots, stale revisions, revoked authority/targets and conflicting evidence fail
  closed. Omitted proofs retain generation evidence rather than losing rollback
  protection. Resolution always also requires current authenticated eligibility.
- `server/DeviceIdentityEndorsement.cs`: matching portable parser and exact
  challenge comparison. This parser does not enroll devices or declare an
  authority by itself; there are no new public request routes yet.
- New isolated native and portable-server regression fixtures exercise real
  identity signatures and Windows DPAPI, not mocked signature success.

### Wire draft v1 (unshipped)

Uses existing mailbox codec primitives: NUL-terminated domain, unsigned big-endian
numbers, four-byte length-prefixed byte/text fields, exact trailing-byte rejection.
Public identity keys are the existing 72-byte canonical P-256 public blobs;
signatures are existing 64-byte P1363 ECDSA/SHA-256. No private keys on the wire.

`PeerChat/account-device-possession/v1`: server fingerprint (64 hex), account
(u64), device fingerprint (64 hex), public key. Signed wrapper
`PeerChat/account-device-possession-signed/v1` contains payload and signature.

`PeerChat/account-device-endorsement/v1`: signed target possession; authority
fingerprint/key; authority generation, target generation, expected directory
revision (u64 each); issuance nonce and device-session binding (64 hex each);
issued/expires UTC milliseconds (u64 each; challenge lifetime at most 60 seconds).
Signed wrapper `PeerChat/account-device-endorsement-signed/v1` contains payload
and authority signature. Max target proof 1 KiB, endorsement 2 KiB.
Challenge expiration constrains enrollment commit, not the lifetime of a durable
endorsed key; current authorization still gates every subsequent use.

Only direct authority-to-member identity endorsements are accepted. No delegated
member signer, history grant, group membership or cache capability is inferred.
The current bounded directory retains up to 32 proof/tombstone entries; exhaustion
fails closed, never evicts trust/revocation evidence to fit another device.

## Validation so far

`tests/test-device-identity-directory.ps1` passed native and portable-server
interoperability at `build/tests/device-identity-directory-d9446a44bb41408d868b01f50ffdfc1f`.
Covers valid possession/endorsement, explicit bootstrap, no-chain denial, wrong
target/account/server/profile, changed session/generation, invalid signature,
truncation/trailing bytes, challenge expiry/replay, revoked/omitted targets,
authority tombstone, stale revision, reopen and injected atomic-write failure.
Also covers two recipients including offline identity resolution, duplicate
proofs, empty same-revision equivocation, and eight concurrent signing attempts
(exactly one succeeds). Review identified the empty-snapshot equivocation and
unsynchronized signing issues; both were fixed and re-reviewed. The local signing
guard does not survive process restart: durable single-use server challenge
consumption remains mandatory before any enrollment endpoint is enabled.
This is a host foundation test. It is not Android JNI, server admission, actual
cached delivery, or package validation.

Portable server Release build passed with the parser included (0 errors, 15
warnings in existing server/cache code). No new enrollment RPC routes are
registered. Initial strict-warning fixture compilation found incomplete aggregate
initializers and misleading indentation; these were corrected before the passing
run above. No failed test is being represented as a pass.

Protected storage detects tampering but cannot independently detect replacement
with an older valid encrypted record. The adapter must provide fresh coherent
authenticated directory state and independently fence stale local session/revision
leases on every admission. Restoring old ciphertext must not restore an old Scope.
No TPM-backed/offline rollback-resistance claim is made. Directory revocation
freshness has the same explicit bounded-online dependency as current eligibility;
do not label a stale snapshot as freshly verified authorization.

## Remaining implementation gate

1. Server transactional authority selection/enrollment and durable single-use
   challenge/outcome handling, including current ownership/revocation checks
   inside commit. The existing remembered-session proof must not be repurposed
   as blanket endorsement authority. Add a separately scoped authority-selection
   proof and explicit client confirmation before exposing a route.
2. Bounded authenticated directory responses and existing event/reconciliation
   integration, restricted to own account/authorized contacts. Enforce scope
   freshness and monotonic generation at the adapter boundary. Scope objects in
   the foundation are trusted adapter inputs, not untrusted JSON parsers.
3. Windows/native and Android/JNI typed operations, profile-bound atomic stores,
   off-lock reconciliation, explicit bootstrap UI and exact generation fencing.
   Compare against existing verified contacts; never overwrite conflicting pins.
4. Wire attachment identity resolution to verified directory evidence without
   changing complete audience, HPKE, revocation, receipts or group voice behavior.
5. Local application/server integration and emulator cached transfers both ways,
   recipient decryption/integrity/durable ACKs, restart and revocation matrix.
   Live existing Pi accounts cannot exercise a newly added server route until a
   separately approved deployment; local fixtures must be labeled distinctly.
6. Full affected builds/package audits, then Windows/Android package handoff.

No account credentials, real-device authorizations or production profiles are
changed by these foundation tests. No completed cached transfer or package-ready
claim is made by this checkpoint.
