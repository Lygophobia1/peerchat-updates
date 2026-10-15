# Android mailbox account re-enrollment — approval proposal

Date: 2026-10-15. Branch `codex/android-groups-voice`. Existing worktree
`C:/Users/Livel/Documents/Codex/peerchat-android-groups-voice`, source `peerchat`.
Baseline `701017fb5d429417633e75904a3a7f5f0904e9f3`.
Regression checkpoint `d4a648ea4b43808e33f7b449312f20adee28e230`.
The final documentation HEAD is recorded in the delivered checkpoint manifest.

**Design and isolated regressions only. No production migration implemented or
performed. No new APK. Phone identity, mailbox private keys, pins, history, outbox,
original registry and Pi application records were not changed.** Approval is required
before implementing the enrollment-aware local metadata path and before registering
a new binding on the phone. This follows the user's explicit approval requirement.

## Root cause and historical explanation

The user recalls original Windows-server Jonah account19, then recreated Linux-server
JonahPhone account4, keeping the same Motorola app data. This exactly reproduces the
implementation's failure: the profile's device-key registry survives account/server
changes, but its signed recipient binding remains immutable and account-specific.
KeyCoordinator compares the signed account19 against current authenticated account4
and rejects at binding_account. Fetch never starts. No key loss is indicated.

Physical dev.160 established source material:

- Device61f70fb01a6055499eed089025e8e8e2850d640b4c4a6ba3ad772168f092b51b.
- Source key ID f523b9d70c6a40fab95d4b379d5d3716, generation2, Accepted.
- Binding account19; signature valid; all key/public/device/generation comparisons
  match; existing AndroidKeyStore opaque private key opens and matches metadata.
- Current authenticated account4, live presence true, authentication generation3.

The signed binding plus the user's history validates this explanation. The original
Windows server database was not available or inspected, so its account username and
historical events are not independently reconstructed from old server records.
Sammy contact approval does not modify the recipient registry.

## Authoritative running Linux-server records — SELECT only

The existing documented SSH connection, with strict host-key checking, located the
running dev.158 server config at
`/opt/peerchat-test/config/4.0.2-dev.158/server-config.json`, database
`/opt/peerchat-test/data/4.0.2-dev.33/physical-client.db` on192.168.1.6.
Read-only SQLite URI mode=ro plus connection-local query_only and a read transaction
were used. No application fetch/purge/ACK routine, mutation SQL, deployment, service
restart, credential export or full database copy was used. Counts are a point-in-time
snapshot and may change through normal application activity.

Observed records:

| Record | Result |
| --- | --- |
| account4 | JonahPhone |
| Motorola device ownership | Exactly account4 |
| Motorola recipient-key rows | None |
| Motorola pending ciphertext | None |
| Other account4 device |35049f3bf26f1d01a7e6c2d0dad2132a646093008d0c2f23f96fa32aa418201b, the Windows/History Sync device |
| Other device active recipient key |59c487f3b7c746dfa93e9ebdb00b30b7, generation2, storage allowed |
| Other device retired key |adf19a928e1b40bf998f97ce086db3be, generation1, storage disallowed |
| Rows from current Sammy device0f1ae8... to account4 |51, all addressed to the Windows device:5 pending and46 receipted |

This is consistent with account-level Stored for delivery referring to the registered
Windows device. It does not prove a particular UI message's complete history, but
there is no current Motorola-targeted ciphertext to correlate or recover from this
running database. We did not inspect plaintext or raw encrypted envelopes. Old expiry
or historical routing cannot be reconstructed from this snapshot. No ciphertext
addressed to another device should be redirected to the phone.

Physical diagnostics establish the phone session's authenticated account4; the server
independently confirms device ownership. Host inspection did not obtain the phone's
session token or execute a fresh identity challenge. Those checks must be repeated
inside the eventual on-device enrollment operation using the existing session.

## Is existing-key registration supported?

**By the existing server/wire protocol: yes, for this empty target-device registry.**
RegisterMailboxKey verifies authenticated account, device ownership, identity
signature and curve validity. It imposes no requirement to generate a new public key.
Its first registration requires generation1 and an empty predecessor. Therefore a
new binding can use the exact existing agreement public/private key, signed by the
unchanged phone identity, with account4 and a fresh random enrollment key ID.

The fresh ID is an enrollment identifier, not a new encryption key or identity.
Source material generation2 remains2. Target enrollment generation1 is independent;
neither is the application's authentication generation3. A distinct target-specific ID separates per-server enrollments even when numeric
account IDs collide, provided the scoped lookup never reuses another server's
accepted enrollment. The wire itself has no cryptographic server-realm field;
consistent trusted-scope enforcement remains mandatory.

**By the current local client model without modification: no.**
DeviceKeyManager.recordRegistration forbids changing an existing signed binding.
DeliveryManager.acceptMailbox requires the exact accepted stored binding. The registry
also rejects duplicate alias/generation entries. Replacing account19 with4, duplicating
an Active key row unchecked, passing a supplemental binding around the exact-binding
guard, or silently decrementing the material generation would be unsafe.

No server endpoint, wire-format or Pi deployment is required for the verified empty
case. The missing piece is a protected, scoped local enrollment reference managed by
the existing native key/registration owners. This is a security-sensitive local
metadata/API extension and is not implemented in this checkpoint.

## Exact proposed procedure, after approval

1. **Read-only preflight on the retained profile.** Recheck the current trusted
   endpoint using existing server trust, account4, Motorola identity, current session
   generation and server-authorized device mapping. Verify the unchanged old binding,
   source key ID/public point and accepted Active source material. Preserve all source
   registry bytes and old proof. Inventory retained generations without activating
   or importing them. Abort on missing/inaccessible/revoked/mismatched material.
2. **Confirm target state again.** The present Pi query shows no phone key rows.
   Recheck before signing. Current mailbox_keys forbids self lookup, so use the same
   narrowly scoped read-only administrative preflight for this recovery; do not
   invent that state in the app. First enrollment is generation1/empty predecessor.
   If a target row appears, stop and review its exact proof instead of overwriting,
   resetting or automatically choosing another generation. Server's transactional
   registration checks provide the final race guard.
3. **Explicit on-phone consent.** Show original account19 binding, current
   JonahPhone/account4, trusted server identity and Motorola fingerprint. State that
   this registers the existing key for future account4 mailbox delivery, preserves
   old data, and grants no account19 mailbox access. Decline/dismissal does nothing.
   Bind approval to the exact endpoint/account/device/source proof and live session;
   stale or changed-session UI approvals are invalid.
4. **Prove both existing private operations.** Existing account/device authentication
   and identity signing mechanisms must validate current account4 authority; sign
   and locally verify the exact proposed binding with the original identity. Where
   fresh reauthentication is required, reuse existing device-bound remembered-device
   challenge/resume, not a new trust system or automatic remembered-key replacement.
   Under the existing original-key operation lease, use established HPKE to seal/open
   a fresh random self-challenge to the retained public point. Bind its local context
   to target trusted server/account/device, original proof digest and proposed proof
   digest; compare and erase challenge plaintext. No create-on-open fallback. This
   HPKE check proves local key possession; it does not grant server/account authority.
5. **Persist one immutable pending enrollment locally.** Use protected native
   metadata owned by existing key/registration code. A proposed `MailboxEnrollment/v1`
   record contains trusted server scope, account4, phone device, fresh enrollment ID,
   target generation1/empty predecessor, exact signed binding/public-point digest,
   source material ID+generation2, original proof digest, consent and Pending state.
   Reference the original key; do not copy private material or create a second
   independent trust/key-state database. Preserve original PKR1 bytes and account19
   proof. Store pending exact bytes before any network submission. Transaction/failure
   handling must preserve both original material and recoverable pending intent.
6. **Submit through unchanged authenticated mailbox_register.** Send only that exact
   account4 binding on the current pinned session. The existing protocol verifies
   device ownership and the identity signature. Never submit old account19 bytes as
   account4 or write the server database directly. On timeout retain Pending and retry
   identical bytes after fresh same-scope authentication. Existing state conflicts,
   revocation, account/pin/device changes or source-material loss stop recovery.
7. **Accept narrowly and maintain existing guards.** Mark only the exact request's
   enrollment Accepted after its authenticated accepted response and native
   reconciliation. The enrollment-aware operation must compare incoming binding
   bytes and target generation against that protected accepted enrollment, then hold
   the original KeyManager material-state/public-point lease through HPKE and durable
   import. Never weaken the current exact-binding check or bypass revocation. Source
   physical-key revocation/inaccessibility blocks all references; enrollment state is
   not an unchecked copied Active flag. Physical-key and per-enrollment revocation
   must be distinct and tested. No second competing DeliveryManager is introduced.
8. **Resume the existing mailbox path.** Only this completed current-scope registration
   enables fetch. Require envelope account4, phone device, enrollment ID/generation,
   trusted current sender, signature/HPKE/AAD validity and durable Storage import.
   Account19 wire must fail even though the same private key could perform ECDH.
   Generate/sign ACK only after the existing durable-import/receipt rules succeed.
   Do not relabel historical device/account attribution or adopt old remote history.
9. **Physical acceptance.** Send a new uniquely named Sammy message with the phone
   offline. Verify the new Motorola-specific binding/recipient ID, server storage,
   sender closure, phone reconnect/import/display/ACK, sender receipt reconciliation,
   restart and duplicate suppression. Check live messages and Sammy trust unchanged.
   Old stored Windows-targeted ciphertext remains Windows-targeted; any resend of
   old text to the phone requires the existing explicit user send/resend action.

Scope is one reviewed target enrollment, not unrestricted cross-account migration.
The implementation must refuse arbitrary UI-supplied source IDs, target accounts,
server pins, revoked material and stale approvals. If historical attribution conflicts
with current server accounts, stop rather than rewriting it. Original local history
remains preserved; enrollment does not authorize new account19 network imports or
archive exports into account4.

## Security implications of reusing the physical key

Account isolation remains enforced by authenticated server ownership, scoped accepted
bindings, signed account/device/key IDs and HPKE AAD. Reusing one physical key does
not create independent cryptographic compromise domains: compromise of that existing
key could affect ciphertext from both enrollments. No arbitrary decrypt/sign API may
be exposed for recovery. The self-challenge is internally generated random data and
returns only a possession result; it cannot accept old mailbox wire as a challenge
or release plaintext. New account4 enrollment grants no account19 authentication,
fetch, import, ACK or history-export authority. This tradeoff is part of approval.

## Rollback and crash behavior

Before submission, decline/failure leaves the source untouched and no target row.
A saved Pending record is never an authorization to receive. After a server accepts
but before local acceptance persists, restart reconciles/retries the identical proof
under the same authenticated scope. It must not mint a new ID or private key.
If local completion fails after server acceptance, keep the pending proof and all
server ciphertext; do not revoke/delete as automatic rollback (revoke purges queued
payloads in the existing server). Returning to the older app must fail closed rather
than discard the new protected enrollment. Backward compatibility and forward-record
retention are required before packaging an enrollment implementation.

## Regressions completed in this checkpoint

New native fixture uses actual Windows SQLite/DPAPI/CNG and shipping HPKE. It
reproduces original account19/gen2 → current4 failure; proves exact protected
registry/history/contact/queue preservation; demonstrates same-key HPKE possession
and low-level account4 decrypt, rejects old-account replay, and proves existing
registration overwrite and durable-receive guards still reject the unimplemented
new enrollment. Synthetic keys only; actual phone keys were never used or exported.

New managed fixture uses the unchanged server AccountDatabase/MailboxProtocol code.
It builds separate original-Windows-style account19 and recreated-Linux-style
account4 databases, registers original generations1/2, retains original account19
ciphertext, then checks new same-public-key account4 generation1 enrollment,
wrong signer/account/generation/conflict rejection, account4 store/fetch/restart/ACK,
and no account19 access or ACK. Original ciphertext remains unchanged on its server.

This is protocol/crypto feasibility, not completed local recovery. The receipt
fixture is identity-signed test data, not a claimed new production durable import.
These host tests do not execute the Linux binary or Android/ART. No new Android build
is necessary for tests/docs only; dev.160 remains the latest physical diagnostic.
Future implementation needs scoped lease/revocation, crash/retry, stale approval,
protected-format compatibility and complete app offline acceptance tests.

## Integration manifest and approval boundary

Main Work's latest committed manifest was reviewed at
`0a7b18ce0b7058decff0808761613edea727305c`. No Main worktree file changed.
This checkpoint adds only tests/runners/docs; no production/shared API or format
is changed. Future likely shared surfaces are mailbox_key_coordinator.hpp,
device_key_manager.hpp, protected/SQL enrollment persistence, delivery_manager.hpp
and portable/peerchat_core.cpp plus narrow Android consent/execution code. Coordinate
these additive interfaces against Main Work before implementation. No group crypto,
attachment envelope, TransferManager, cache or voice change belongs in this recovery.

**Approval requested:** implement and test this single-account enrollment reference
and explicit on-phone consent flow, preserving the existing physical key and all
original data. Do not execute actual enrollment until the user confirms the exact
on-phone target operation. No direct server SQL modification or key rotation is
part of this request. Any discovered need for a server/protocol change or different
recovery requires a new review before proceeding.
