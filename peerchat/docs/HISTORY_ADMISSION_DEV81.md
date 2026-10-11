# History admission diagnostics — dev.81

This is a diagnostic checkpoint, not physical History Sync acceptance. No trust,
grant, identity, history, server schema or archive transfer policy was changed.

## Proven existing-profile failure

Read-only SQLite/DPAPI inspection of the existing Jonah profile and SQLite
READONLY inspection of the running Pi server's configured database agree:

Account: 3. Devices:

- A (Jonah): `0cb421c4f1023734edf49f05cdda81c7a0381a667b0f1d1537dbb9c6bd6c9ee4`
- B: `0612f25864074654f3fca0bf9b8bf95fce52ba52bab187a6efcee799d58cfb6e`

| Authorizer | Subject | Generation | Permissions | Revoked / server disabled | Jonah local trust |
| --- | --- | --- | --- | --- | --- |
| B | A | 3 | Receive + Provide | No / No | Trusted |
| A | B | 1 | Receive + Provide | No / No | Disabled |
| A | A | 5 | Provide only | No / No | Disabled |
| B | B | Missing | None | Not applicable | B is trusted |

The three local grant IDs exactly match server IDs; all local signed grants
verify, both account/device descriptors exist on the server, and there are zero
pending local publications. No stale grant-generation discrepancy was found.

For B -> A, trusted B lacks B's Provide grant. A's grant pair cannot be admitted
because A is explicitly disabled on Jonah. For A -> B, trusted B lacks B's Receive
grant, and disabled A cannot authorize this direction either. Individual device
authorization is not proof of a same-authorizer source/reader pair.

This establishes a failed condition on Jonah, not B's complete local policy.
B's protected local trust state could not be inspected: ADB detected no phone.
No remote policy was guessed from server membership.

There is no safe automatic data correction: enabling disabled A or signing a
missing B grant would make a new authorization decision. Existing valid B -> A
generation 3 remains intact and recognized. User consent was requested for an
explicit permission action on B under the already-trusted B authorizer. No grant
was issued or silently reissued, and no authorizer was automatically approved.

## Diagnostic boundary

Local application operation 22 returns a <=49,152-byte read-only snapshot. It
requires the exact local account/authenticated generation, but works with stale
metadata so stale admission is observable. It never refreshes, accepts or issues
grants. References cover local device/account, issuer trust, device descriptors,
local/server IDs/generations/capabilities, signature verification, eligibility,
revocation, authentication/reconnect generation, metadata revision and age.

Permission checks and diagnostic reasons use the same predicate; no alternative
authorization implementation was added. Reports distinguish unknown/disabled
authorizer, missing local/current grant, permission mismatch, generation/ID
mismatch, ineligible/revoked grant, stale metadata and incompatible issuers.

Windows captures the immutable result on the existing archive worker, including
failures, exposes it in Advanced History Security and full/logical diagnostics.
Android uses the existing MessagingBackend and projects the native result into
application diagnostics. No keys, proof bytes, credentials or message/attachment
contents are included. The optional existing-profile probe opens SQLite READONLY
and never constructs Storage/Profile or exports/accesses a private key.

## Automated validation

- Enrollment/security: unknown/disabled/mismatched authorizers, wrong-account and
  stale-generation rejection, revoked/removal handling, exact publication/restart,
  read-only diagnostics, incompatible issuer pairs, local/server comparisons.
- Storage/provenance: bidirectional actual native import under existing valid
  test grants, durable cursors, disconnected/restarted catch-up, replay/dedupe,
  whole-page tamper rejection and no outbox/fabricated ACK.
- Packaged Windows: actual paired Node workers, zero friendship, opaque relay,
  mutual Schannel TLS, 25-message catalog transfer, verified attachment download,
  five profile-worker restarts, protected identity and grant persistence;
  operation-22 parsing and public references survive privacy filtering.
- Server metadata and 12 real mutual peer-TLS archive handshakes passed.
- Existing History Settings smoke passed.
- Android JVM tests/debug/test/release APKs/lint passed; ARM64/JNI/profile/TLS
  dependencies and release test-hook/credential exclusion passed.
- Full Windows build/package/import checks passed; Development version/hash/
  safe updater extraction passed in Windows PowerShell (.NET Framework host).
  The first PowerShell 7 updater invocation could not deserialize using the
  Framework-only serializer; no production change or assertion weakening.

## Artifacts and remaining gate

Windows: `build/development/4.0.2-dev.81/Client/PeerChat.Desktop.exe`

Android: `build/development/4.0.2-dev.81/Android/PeerChat-4.0.2-dev.81-debug.apk`

APK SHA-256: `F3C4740418ABA1BC9FFF10B345A6369E3A9FD1B4ACEA2207EF063AC7790219B9`

Validated Development ZIP:
`build/development/4.0.2-dev.81/DevelopmentUpdaterValidated/PeerChat-4.0.2-dev.81.zip`

SHA-256: `5c32f7dfd289c2774e2671f5f913b6416a546d1e0cfad392cc00b410b2b97670`

Unsigned Development policy only; no upload, publish, phone replacement install,
server deployment, merge or physical bidirectional transfer acceptance claimed.

Next gate: inspect B's admission snapshot; resolve the genuinely missing grant
through explicit user action only; then verify both physical directions, restart,
reconnect catch-up, preserved grant generations/IDs and duplicate prevention.
