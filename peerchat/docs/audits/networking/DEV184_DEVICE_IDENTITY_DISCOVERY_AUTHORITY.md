# dev.184 authenticated device identity discovery — authority boundary

Date: 2026-10-18. Investigation baseline: `3a2de3d`, existing
`codex/native-unicode-emoji` worktree. No production implementation or server
deployment in this checkpoint. Group voice work is unchanged.

## Decision

The current test-account evidence does **not** contain a trusted cross-device
authorization chain for the missing Windows identity. Do not promote server
directory entries, self-signed recipient keys, or successful password login into
locally verified contact pins. Implementing automatic verification for this case
requires an explicitly scoped enrollment extension and a bootstrap-authority
decision. This report proposes that extension; it does not enable it.

## Existing evidence and limits

| Material | What it authenticates | Why it cannot alone establish the missing contact trust |
| --- | --- | --- |
| Remembered enroll/resume challenge | Possession of the current device key, exact account/session/purpose and single-use challenge | Self proof to the server, not an endorsement from a previously trusted contact device; enrollment returns a credential, not a contact-verifiable delegation certificate |
| Device inventory/current eligibility | Current server-authorized account mapping, revision, revocation and recipient-key eligibility | Necessary current authorization, not independent cryptographic approval by an already trusted device |
| Mailbox recipient binding/cache capability | Device signature binding its HPKE key, account, generation and supported format | Verification still needs the trusted identity signing key; self-signature is not the trust root |
| Archive authorization grant | Trusted archive issuer can authorize a target key for history receive/provide | Domain/capabilities are archive-specific; not general DM identity delegation or permission to enroll other devices |
| Group device approval | Signed actor/target keys and authorization within one exact group/roster generation | Group-specific admission and protected ledger receipts; must not silently become account-wide DM trust |
| Existing verified contact | Exact public key/fingerprint retained locally | No current authority bit granting that device unrestricted delegation for other devices |

Relevant code: `server/PeerChat.Server.cs` (`EnrollRemembered`,
`VerifyRemembered`, `DeviceAuthorityRevision`, `NotifyDeviceManagement`),
`server/ArchiveAuthorization.cs`, `group_device_binding.hpp`,
`group_device_replica.hpp`, `mailbox_protocol.hpp`,
`portable/peerchat_core.cpp::mailboxContact`, `node.cpp` DM evidence resolver.

The existing group and archive proofs contain useful cryptographic building
blocks, but extending their purpose would be a security-policy change. None may
be repurposed merely because its signature validates.

## Disposable account observation

Read-only Pi SQLite inspection used URI `mode=ro` and `PRAGMA query_only=ON`,
selecting only public account-14 authorization metadata. On this observation,
`archive_device_grants`, `group_device_proofs`, and
`group_device_identity_proofs` contained **zero rows for codex1**. No proof blobs,
credentials, private keys or personal-account records were read.

The older device remains
`a4cf39bd208f304a31d5a17f3715b8dafefb00d2d068a0597dbe0390706d13b3`
(Windows dev.183). Prior authenticated inventory confirmed active, unrevoked,
offline, storage-enabled generation 1 and cache-capable. Its public account
mapping exists, but the Android sender has no protected contact record for it.
The live cached job therefore stops at `prepare_recipient_identity` before upload.
No code change can manufacture a missing endorsement from the older device or
an existing trusted authority. No account/device authorization was changed.

## Smallest proposed extension — approval required

### Authority bootstrap and scope

Use existing identity keys and cryptography; add a separately domain-separated
**account device identity endorsement**, not a new attachment format or engine.
The endorsement identifies a target as a device of an exact account; it grants
neither history access, group membership nor cache access by itself.

An existing contact pin alone must not imply unlimited enrollment authority.
One-time explicit verification should establish the account's chosen enrollment
authority fingerprint and its precise delegation scope. A contact may instead
explicitly verify an individual target, without granting delegation. The first
authority requires out-of-band verification or another already accepted authority
chain; a password/server response cannot bootstrap it silently. Legacy devices
are grandfathered only as existing individual pins, not as new signing authorities.

For the minimal first version, permit one-hop, nondelegating target endorsements:
a verified account enrollment authority can endorse member devices, but members
cannot endorse others. Authority replacement/recovery requires explicit fresh
verification and must not be inferred from account login. This avoids an unbounded
certificate graph and limits compromise propagation. Whether this bootstrap
policy is acceptable is the security decision needed before implementation.

### Signed evidence and acceptance

The canonical signed record must bind protocol/domain version, deployment/server
identity, account ID, authority fingerprint/key, target fingerprint/key, target
proof of possession, purpose, immutable issuance ID, and monotonic authorization
generation. Enrollment approval must itself bind a fresh single-use challenge and
expected current revision. Never expose arbitrary signing through JNI or WPF.

Persist the public endorsement and provenance with the local verified identity,
encrypted under the existing profile protection. Retain the highest accepted
authority generation/revocation revision durably; commit pin, proof and watermark
atomically before considering discovery complete. An unrelated directory rename
must not invalidate a valid immutable key endorsement: identity generation and
directory revision have distinct semantics.

Before each new delivery, require both a valid local endorsement chain **and**
current authenticated account/device/key eligibility. Reject unknown authority,
wrong purpose/account/server, public-key hash mismatch, target substitution,
invalid signatures, stale generation, revoked authority/target, and late replies
from a replaced profile/session. Never overwrite an existing conflicting pin.
Revocation blocks future use without deleting history or redirecting queued data.
If fresh current authorization cannot be established, retain pending work rather
than treating an old certificate as current authorization.

### Directory reconciliation

Transport bounded public endorsement records through an extension to the existing
authenticated device-directory responses. Reuse event hints and startup/login,
reconnect and authority-revision reconciliation; events request refresh and are
not themselves proof. Own-account `device_management_changed` already exists;
contact-facing notifications/responses must remain restricted to authorized
relationships and carry the exact account/revision. Missed events are repaired
through bounded snapshot reconciliation, not per-message polling.

Verify off the UI thread and outside application locks, then recheck profile,
session and authority generations before atomic persistence/publication. Existing
pins take the fast path. Attachment preparation consumes the resolved identities
and strict complete audience; it does not initiate a separate trust system.
Unknown/unendorsed targets expose one-time verification required, never an
upgrade-required fiction or silent recipient omission.

### Compatibility and rollout

No change to ciphertext formats, HPKE, TransferManager, message IDs or ACK
semantics. A versioned directory capability is needed. Old servers/clients keep
their existing explicit verification behavior. An offline legacy target may be
endorsed only using authenticated target evidence actually available to the
authority; otherwise it must connect once for proof/verification. Merely knowing
its fingerprint or that it is registered is insufficient. No automatic re-enroll,
key regeneration, revocation or migration of current real devices.

Server code and public evidence storage would require a subsequent reviewed
implementation and separate deployment approval. This is not a client-only
readiness fix. Older clients must not be reported as having consumed the new
directory contract.

## Validation boundary and required integration

The earlier checkpoint passed unrelated-contact audience invariance, cache
security/durability host regressions, and selected-device live Windows/Android
DM ACK plus restart persistence. Those do not prove cached decryption or account-
wide completion. The new enrollment extension has not been implemented or tested.

This investigation reran `tests/test-device-verification.ps1`: PASS (real host
Schannel). It covers explicit acceptance/rejection, stale authentication,
fingerprint mismatch, expiry, retired attempts, revoked friendship, persistent
pins after reopen, preservation of old queued recipients, and exact retry/ACK/
deduplication. This validates the existing fallback boundary, not offline discovery
or live cached transfers.

After authority-policy approval, test canonical proofs and negative cases first:
multi-device/offline audiences; no-chain legacy devices; substitution, cross-account
and cross-server replay; replacement/conflicting pins; revoked signer/target;
generation rollback; interrupted atomic pin/proof persistence; stale login replies;
lost events and restart. Then use codex/codex1, including the older dev.183 device,
for actual cached transfers both ways: upload, sender offline, each authorized
recipient's decryption/hash verification, signed durable receipts and sender
reconciliation. Preserve queued jobs and require all original target outcomes.

Do not declare that live matrix passed or prepare a purportedly repaired package
until the missing authority is legitimately established. Do not alter test-device
authorizations merely to make the fixture green.
