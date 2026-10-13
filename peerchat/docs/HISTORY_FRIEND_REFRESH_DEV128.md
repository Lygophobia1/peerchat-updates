# Friend reconciliation must not revoke archive authorization — dev.128

Physical Windows/Android History Sync remains FAILED / PENDING acceptance.

Both Windows AuthenticatedFriends and Android ReliableBridge.friends invalidated
the independent archive metadata even for the same authenticated account and
generation. Android also closed every endpoint absent from the friend list,
including authorized own-account archive devices (which are not friends).

An unchanged-snapshot regression failed against the dev.127 Windows package at
the next native archive operation. On the real Motorola, the already-ready
JNI/Keystore/TLS archive endpoint changed from ready=1 to ready=0 immediately
after the unchanged friend snapshot (assertion at NativeArchiveRouteTest:72).

The narrow correction invalidates archive evidence on genuine account or
authentication-generation changes, not ordinary same-context friend refresh.
Android keeps an archive endpoint only if its own current verified archive
authorization remains valid; friendship cleanup still removes revoked friend
routes. Expired metadata and revoked/mismatched grants still fail closed. There
is no automatic trust, grant issuance or new lifecycle/retry owner.

Validation:

- Actual packaged Node workers now pass the same unchanged friend refresh during
  TLS pairing, verified 25-message archive import and encrypted Unicode attachment
  transfer. Five profile-worker restarts, wrong accounts and genuine logout/new
  authentication context still require fresh metadata.
- Motorola: five real JNI/Keystore/BC TLS profile lifetimes pass unchanged refresh
  preservation AND genuine newer-generation invalidation. Existing test signing
  identities persist through reopens. Namespaced test profiles only; no normal
  identity, grants, remembered authorization or history was altered.
- 141 JVM/UI tests, debug/test/release, lint, ARM64/JNI/dependency/release audits.
- Native grants/enrollment/revocation, 67 verified imports/cursors/replay/races,
  mutual TLS archive and group-purpose isolation pass.
- Full Windows build/package/import, unified version and dev.127 -> dev.128
  Development updater hash/extraction audits pass; no publication or merge.

Dev.128 is replacement-installed on the Motorola. Physical normal-profile
reports still show zero imports: Windows-to-Android completes an empty scan;
Android-to-Windows connections get through authentication/scope binding and then
Android records source-stage archive_control rejection. That is a subsequent
source-side record/control failure, not acceptance of the complete milestone.
Source rejection granularity is the next diagnostic boundary. Permissions and
grants remain current and unchanged; both clients remain authenticated.

Candidates: build/development/4.0.2-dev.128/{Client,Android}.
Android debug SHA-256:
CD1B0A345FE369DEEFBC6E92B3656B381EB876654FEEBD12EA62D5F5441AE423
DevelopmentUpdaterValidated/PeerChat-4.0.2-dev.128.zip SHA-256:
a5158320b15446396e0a451049bd62dbabcfc283e45df4d1ff7c631690a3a4a4
Development unsigned policy only. Pi remains the corrected compatible dev.125;
these are client-only fixes, not a reason to deploy a version-only server update.
Prior Pi binary/config/SQLite rollback backups remain preserved.
