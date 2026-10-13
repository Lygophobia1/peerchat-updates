# History Sync connection ownership repair — dev.130

## Scope and outcome

Continued Astra's uncommitted work from `ab570f4` on
`codex/network-cleanup-phase1` in the existing `network-cleanup-phase1` worktree.
Two independently reproduced lifecycle defects are corrected. Existing signed
grants, device/account mapping checks, archive protocol, import verification and
durable cursor formats are unchanged. No Pi deployment or historical identity
migration was performed. No normal profile, key, grant or history was modified.

This checkpoint is a validated development candidate, **not full physical
Windows/Android History Sync acceptance**.

## Confirmed defects and corrections

1. `Execution::disconnected` previously identified a pending request by archive
   scope only. A retired socket's late cleanup therefore cleared a newer
   winner's pending request in the same scope. The next request changed its
   correlation ID, making a valid response fail admission. The regression failed
   before the correction with `Retired archive attempt cleared the winner's
   pending request`.
2. The response deadline previously closed the scope's current winning transport,
   even when the expired request belonged to a different, retired transport. The
   regression failed before the correction with `Old archive request deadline
   retired the replacement winner`.

`Execution` now retains the transport attempt that owns its pending request.
Cleanup releases backpressure only for that owner. Deadline handling closes and
retries the owner only if it is still the winner; a replacement stays available
and the stale request is released for reissue. The 15-second deadline still closes
an actual unresponsive owner and schedules the existing bounded retry.

Bounded diagnostics add `pending_request_attempt`, `stale_cleanup_ignored` and
`stale_deadline_ignored`. They contain public identity/scope/attempt references,
not message content, credentials or keys. Owner state is cleared on completion,
pause, cancellation, authorization loss, rejection and job replacement.

## Changed files

- `archive_execution.hpp`: pending request ownership, cleanup, deadline and
  diagnostic handling.
- `node.cpp`: Windows request and connection cleanup pass their actual attempt ID.
- `portable/peerchat_core.cpp`: Android JNI request and all archive cleanup paths
  pass their actual endpoint/attempt ID.
- `tests/archive_enrollment_test.cpp`: retired cleanup, replacement request,
  stale deadline and genuine owner deadline/retry regressions.
- `tests/archive_reliable_endpoint_test.cpp`: inject stale cleanup while an
  authenticated TLS winner imports nonempty bounded history pages.
- `android/app/src/androidTest/java/com/peerchat/android/NativeArchiveRouteTest.kt`:
  nonempty bidirectional JNI/TLS/import/reopen/deduplication fixture.
- `shared/VersionInfo.cs`: `4.0.2-dev.130`.

## Validation performed

Passed on the Windows host:

- `test-archive-security.ps1 -Enrollment -PeerTls -HistoryStorage -SessionScope
  -Provenance`: both defect regressions, legitimate owner timeout, signed consent,
  stale/revoked/wrong-account grants, missing mappings, whole-page verification,
  67 durable history imports, reverse-direction imports, restart/replay and
  deduplication. Twelve real mutual-TLS handshakes and 25 records across four
  bounded TLS history pages passed with stale cleanup injected.
- `test-archive-security.ps1 -ServerMetadata`: isolated authenticated server
  metadata and pinned-TLS relay tests passed for both directional scopes,
  cancelled pairing, repeated retry, duplicate rejection preserving a winner,
  cleanup, retained authentication and legitimate session supersession. This ran
  against an isolated host server, never the live Pi.
- `test-archive-application-boundary.ps1` against the dev.130 client: actual Node
  workers, 25 committed imports, Unicode attachment catalog/download, pending
  pairing retirement, same-generation friend refresh and five worker restarts.
- `test-archive-attachments.ps1`: metadata protection, CAS/reopen, scoped
  attestation, tamper/revocation and immutable logical message identity.
- `test-archive-settings-ui.ps1`: existing settings/consent/persistence smoke.
- Windows client/updater/server compilation and package load/import/dependency
  audits. The unchanged dev.129 ScreenMedia DLL was copied with identical SHA-256
  `66541E0DFA287FDD4545C00C2103CA9293C5BABD395B151F37D0EB043D942F01`;
  the three pre-existing untracked media object files were left untouched.

Passed for Android:

- 141 JVM/UI tests, zero failures/errors.
- Debug, instrumentation-test and release APK compilation; lint.
- Final instrumentation-test recompilation/lint after per-direction import-count
  assertions were added.
- APK version, debug signature, ARM64-only native payload, archive JNI export and
  expected native dependency audits. Dev.129/dev.130 debug signing certificates
  match exactly.

The new `nonemptyBidirectionalImportsSurviveReopenWithoutDuplicates` device test
is **compiled, not executed**. It creates only two randomly named isolated test
profiles, queues 13 Unicode/multiline messages per profile, runs the production
JNI scheduler, Bouncy Castle peer TLS, signed enrollment, native importer and
protected SQLite paths, and checks 13 new imports in each direction. Across three
reopen cycles it checks exact IDs, authors, recipients and text, 26 unique rows
on each device, stable identity keys and zero new imports during replay. Network
forwarding is an in-process byte bridge; real Pi forwarding and physical
Windows/Android operation remain separate acceptance gates.

No Android installation or launch was performed during this repair. The user now
owns physical APK installation/testing; automatic ADB installation, application
launching and repeated USB troubleshooting are prohibited unless specifically
requested.

## Remaining provenance boundary

The read-only evidence already recorded at `ab570f4` remains applicable:
`HISTORY_LEGACY_MAPPING_DEV129.md` documents four historical device references
affecting 236 indexed rows without authenticated current bindings. At least one
has documented old-server account context different from current account 4.
Index counts are not proof of per-record ownership. None was mapped, reassigned,
deleted, filtered or imported by this task.

The source continues to reject an unattestable record with
`source_record_device_mapping`; the import cursor does not silently advance past
it. The connection fixes cannot authenticate missing historical ownership.
Normal-profile full sync can therefore remain blocked even after transport
recovery. No verified-record-only bypass, migration, grant reissue or misleading
full-completion status was introduced.

## Packages

Base directory:
`C:\Users\Livel\.codex\worktrees\network-cleanup-phase1\Create 2D Game\peerchat\build\development\4.0.2-dev.130`

- Manual-install APK: `Android\PeerChat-4.0.2-dev.130-debug.apk`
- Device test APK: `Android\PeerChat-4.0.2-dev.130-tests.apk`
- Unsigned release build: `Android\PeerChat-4.0.2-dev.130-release-unsigned.apk`
  (build/audit artifact, not the installation candidate).
- Windows executable: `Client\PeerChat.Desktop.exe`
- Windows runtime package: `PeerChat-4.0.2-dev.130-windows.zip`

Debug APK SHA-256:
`7F989E8819840090D727FE18556012E2F81A31399FE62CF5B5AF81AAD4635D25`

Windows ZIP SHA-256:
`AE31AAD931F0856253A94977B26D0705B9DE8B55D518ECC8710DFD2E61677B49`

## Manual acceptance checklist

1. User replacement-installs the debug APK with `adb install -r`, then opens
   Windows dev.130 with the same existing profile. Confirm remembered sign-in,
   existing history and unchanged device references.
2. Sync Windows → Android and Android → Windows using existing valid grants.
   Check committed message counts, original authors, exact multiline content and
   live history updates. Do not treat pairing or an empty scan as import success.
3. Repeat Sync now, restart both clients, and sync again. Previously imported
   messages must remain present exactly once, and Android must remain signed in
   through retries and temporary archive failures.
4. Capture the full execution diagnostics if a source rejects a record. An
   unresolved legacy mapping remains a separate known blocker; do not change
   identities, grants or mappings to force completion.

The isolated Android fixture can also be run manually after replacement-installing
the test APK:
`adb shell am instrument -w -e class com.peerchat.android.NativeArchiveRouteTest
com.peerchat.android.test/androidx.test.runner.AndroidJUnitRunner`
This validates the isolated JNI fixture, not normal-account physical acceptance.

Stop after this checkpoint and wait for the user's physical test results. Group
UI, media and transfer roadmap work remains outside this assignment.
