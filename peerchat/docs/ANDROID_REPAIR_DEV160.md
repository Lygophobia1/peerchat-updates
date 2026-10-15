# Android dev.160 — read-only retained mailbox binding inspection

2026-10-15. Existing branch `codex/android-groups-voice`, worktree
`C:/Users/Livel/Documents/Codex/peerchat-android-groups-voice`, source directory
`peerchat`. Baseline `59f6b81501b1fae5ee3b6fb5eb0673ad955180ae`.
Compiled/validated source: `e7749c6b33e959e838fde2efaaf97a043e55eb13`.

**No mailbox recovery or metadata repair is claimed.** This is a replacement-installable
read-only diagnostic candidate. The actual failing retained-binding check must be
observed on the Motorola before proposing recovery. No keys, pins, histories,
server ciphertext, accounts, consent or trust were reset/rotated/replaced. No phone
installation, Pi operation, Main Work change, merge or deployment was performed.

## Confirmed physical state and scope

The user confirms live messaging works **both directions** with Android open, and
Sammy's verification prompt/approval works. Failure is specifically Windows Stored
for delivery while Android is offline/closed, followed by no delivery on reopening.
Preserve those working live and verification paths; neither is edited here.

Current authenticated account is4. Motorola identity:
`61f70fb01a6055499eed089025e8e8e2850d640b4c4a6ba3ad772168f092b51b`.
Sammy's Windows device:
`0f1ae8dffc8069987b7d772e5f6f8398ed17ea4306f47c78453636e0a0347391`.
The user reports recent incoming records addressed to the **current** phone identity.
This narrows the earlier device-routing uncertainty; the actual encrypted envelope's
recipient key identifier has not been supplied and is not inferred.

The user recalls the same account/server throughout this installation. A previous
account/server is **not an established cause**. The account mismatch regression is
a way to test a check, not evidence of the user's account history.

## What dev.159 proves, and what remains unknown

`native_registration_stage=binding_read_validate` is emitted after:

1. Native storage/delivery/account preconditions succeeded.
2. The existing protected registry was read, decrypted, decoded as PKR1, and its
   metadata validated (role, suite, alias syntax, key point representation, state,
   generation bounds, registry uniqueness and registration state).
3. An Active key's nonempty signed binding was found.

Thus this attempt is **not fresh key provisioning, an absent registry, or an opaque
protected-registry access failure**. The earlier empty-registry/orphan-key candidate
is excluded for this attempt. This does not prove that the private key still opens:
that operation was not used to validate an existing binding in dev.159.

The old compound branch then calls parseBinding and requires matching metadata key
ID/public point/generation plus verifyBinding against the current account, current
identity fingerprint, current public identity and signed binding payload. Any of
those failures previously had the same stage. The provided report does not identify
which one failed. No actual phone registry or private key is accessible from this
host; no fabricated inspection or root-cause repair is claimed.

The native exception is caught and converted to a null registration. Android keeps
registeredBinding unset and skips mailbox_fetch. native_receive_stage=not_requested
confirms no mailbox receive operation was reached. The visible mailbox_keys
IllegalStateException is later **outgoing** remote-key discovery returning the
server's rejected_or_storage_unavailable code; it is independent of local binding
validation. Do not interpret it as the phone's key-registration server rejection.

## Diagnostic changes, with original guards preserved

`mailbox_key_coordinator.hpp` now labels the exact existing checks:

| Stage | Operation/failed check |
| --- | --- |
| binding_parse | Existing canonical domain/version/suite/length/signature representation parser. |
| binding_metadata_key_id | Signed binding key ID equals retained metadata ID. |
| binding_metadata_public_key | Signed recipient public point equals retained metadata public point. |
| binding_metadata_generation | Persistent recipient-key generation equals registry generation. |
| binding_identity_read | Read the existing identity public representation/fingerprint. |
| binding_account | Signed account equals current authenticated account4. |
| binding_device | Signed device equals the current local identity fingerprint. |
| binding_identity_fingerprint | Existing identity/fingerprint consistency check. |
| binding_signature | Existing verifyBinding cryptographic check succeeds with the current identity. |

No rejection guard is removed. Diagnostics include only public IDs/numeric values
and fixed equality results: metadata key ID/generation/registration, signed account,
device/key ID/generation, canonical binding/schema result, metadata public-point
comparison, creation/predecessor comparison, current identity fingerprint, account
and device matches, and an independent signature-valid boolean when reachable.
They never include private/public key bytes, signatures, protected blobs or aliases.
PKR1 and binding schema1 are reported only after their parser has succeeded.

`native_authentication_generation` is the current application session generation.
It is **not a field of the persistent signed binding**. binding_generation and
metadata_generation are recipient-key generations; comparing either with session3
would be incorrect. No such comparison or recovery was added.

On registration failure, native core makes a read-only openRecipient probe of the
exact retained metadata ID. It never creates a key or changes binding metadata:

- available_matching_metadata: existing opaque handle opens and its public point
  equals the protected metadata. This is not yet proof of decrypting a stored wire.
- unavailable_or_mismatch: handle access, policy, alias or public-point comparison
  failed; retain the fixed platform reason for further distinction.
- not_checked: no eligible retained metadata was reached.

Configured provider is AndroidKeyStore. Its existing provider enforces the current
profile alias namespace and agreement-only P-256 policy when opening; aliases are
not exported. Diagnostic state is cleared before provider setup so previous attempts
cannot supply stale key IDs. Existing registration readiness still gates fetch.

After a future successful registration, parsed incoming envelopes report their
public recipient account/device/key ID and sender device for correlation. No fetch
is made merely for diagnostics while registration remains invalid. For this
checkpoint the server's actual retained key ID/ciphertext correlation is unknown;
Windows Stored plus the reported recipient identity does not prove key-ID equality.
Stored pre-repair messages remain untouched, not promised recovered or deleted.

## Could Sammy approval change this local binding?

Source trace: existing approval calls reliable::persistAuthenticatedContact, which
writes the peer contact to ordinary message Storage. The mailbox registry is a
separate protected SQL database; that helper has no registry/KeyCoordinator mutation.
A new isolated regression calls the actual persistence helper and asserts identical
protected registry bytes before/after. The current source does not support the
hypothesis that accepting Sammy modifies the local binding. No verification UI,
trust operation, outgoing message behavior or live receive behavior is changed.

## Focused validation

- 203 Android host tests passed, zero failures/errors/skips. Lint passed with
  zero errors/fatal issues and25 existing warnings. Results are in package-evidence.json
  and the copied XML/log evidence. ARM64 debug and instrumentation compile; ART/phone
  instrumentation execution was not performed. No release package created.
- Precise-binding regression uses actual SQLite/DPAPI/CNG, tests canonical parse,
  metadata ID/public/generation mismatch, wrong account, wrong device, corrupted
  signature, a cryptographically valid wrong-account binding, diagnostic reset and
  contact approval leaving registry bytes unchanged. The original key is retained;
  restoring the exact original metadata makes the existing binding usable again.
  This is an isolated regression, not a repair applied to the phone.
- New native offline regression: recipient initially closes; Windows/native sender
  stores ciphertext then closes; recipient reopens its profile AND real protected
  SQL registry, validates the identical binding, survives an interrupted fetch,
  decrypts/commits history and creates its signed receipt; ACK is lost; another
  profile/registry reopen repeats the same wire and yields one history insertion
  plus the identical receipt; sender reopens and reconciles exactly once. Passed
  both Windows BCrypt and Android's pinned MbedTLS signature verifier with shipping
  HPKE. Server/auth transport in this native fixture is explicitly an immutable disk
  spool plus authenticated test metadata, not a real Android application session.
- Expanded actual isolated pinned-TLS service regression disconnects recipient
  before store, logs sender out, reconnects recipient, re-registers the unchanged
  binding, rejects a failed pinned connection without deleting ciphertext, fetches
  the same pending envelope repeatedly, ACKs and reconciles the sender receipt.
  This service fixture uses native-generated wire/receipt fixtures; decryption and
  recipient profile persistence are proved by the separate native regression above.
- Android execution regression injects a real IOException at fetch: no native receive
  or ACK occurs; retry succeeds without replacing or repeating local registration.
- No integrated Motorola physical offline acceptance is claimed by these tests.

## Integration and checkpoints

Main Work's committed manifest at `96de4692962f0a8e8ec38e6fa384028f0f200ac5`
was inspected again before editing shared code. Shared production files modified:
`mailbox_key_coordinator.hpp` (diagnostic accessors/stages) and
`portable/peerchat_core.cpp` (fixed-stage/public inspection text and retained-key probe).
C ABI, JNI signatures, wire/schema/key formats, account/trust policy, provider key
creation, registration acceptance, fetch/import/ACK decisions and server operations
are unchanged. Only diagnostic text expands; package native code with matching app.
No group crypto/attachment/cache/voice dependency or conflicting architecture is added.
Manual integration later must preserve both branches' portable core additions.

- `bb66c99`: retained-binding diagnostic checks and read-only opaque-key probe.
- `65af064`: offline recipient/server restart regressions.
- `e7749c6`: transient fetch IOException preserves registration and prevents premature ACK.
- Final documentation hash is in package-evidence.json.
  Final working-tree cleanliness and exact compiled-source correspondence are recorded
  there after checkpointing. Documentation-only commits do not change the APK.

## Package and next physical evidence

`PeerChat-4.0.2-dev.160-retained-binding-diagnostics-debug.apk`,
com.peerchat.android, versionCode2, ARM64, minSdk26/targetSdk35.
SHA256 `ac66ee18bf76580d224a7a8d0c3c01e5539fc526c3c07975a620fd4a90532588`.
Verified signing certificate SHA256
`db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`,
matching dev.155/dev.158/dev.159. Same signing identity supports replacement install;
actual on-device preservation is for the user to verify. Earlier packages retained.

1. Install over dev.159 without uninstalling, clearing data, changing accounts,
   toggling mailbox consent, resetting trust or deleting queued messages.
2. Reopen and allow normal authentication. Copy the complete messaging diagnostic
   section, especially native_registration_stage, native_recipient_key_access,
   binding_account/device/key IDs and all *_matches / signature-valid values.
   Compare the printed local identity with the current Motorola fingerprint above.
3. If registration still fails, return these lines. Do not attempt recovery/reset;
   this is the evidence needed to establish the actual check and discuss a safe plan.
4. If normal registration succeeds, repeat offline Windows send → Stored → Windows
   fully closed → phone reconnect. Check fetch/import/ACK and one durable visible
   message after app restart. Retain old stored messages and check them separately;
   do not re-encrypt or automatically retarget historical encrypted rows.
5. Confirm live messaging/accepted Sammy trust remains intact. Stop for review.

Any future metadata/key recovery requires the demonstrated failure, comparison with
authenticated original binding and existing key, retained old ciphertext/metadata,
and an agreed user-authorized plan. No automatic recovery path is introduced here.
