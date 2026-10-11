# History recovery checkpoint — dev.80 (milestones not accepted)

Baseline: 6c8b48a / dev.79. Compatibility defaults, explicit saved backend
preferences, dev.78 diagnostics and dev.77 Settings metadata scope are preserved.
No server deployment, profile cleanup, grant replacement or identity change.

## Proven reliability defects corrected

1. Desktop PumpArchive and Android MessagingBackend startup used RefreshCurrent,
   which only fetches metadata. Neither registered the existing durable local
   descriptor unless Settings was opened. Startup now uses PublishPending, which
   registers that exact descriptor and publishes only already-durable signed
   grants. It does not approve an authorizer or issue a grant.
2. Both application pumps stopped reconciling after idle/failure. They now perform
   bounded idle reconciliation every 60 seconds and active refresh every 10
   seconds, using their existing worker. A failed initial attempt is not marked
   loaded and retries are bounded. No additional reconnect owner was created.
3. Execution treated durable completion exactly like explicit Pause, suppressing
   incremental automatic scans indefinitely. New protected ArchiveIntent/v2
   distinguishes paused, running and completed. Completed scans reconcile at
   most once per 60 seconds, retaining per-source/conversation cursors and rotating
   through eligible sources. Current verified grants and automatic-sync flags
   are checked before each start.

Legacy ArchiveIntent/v1 false cannot prove whether the user paused or completed.
It remains stopped rather than silently undoing Pause. One explicit Sync now
upgrades the scheduling intent; no grants/history/cursors are recreated.
This is an unresolved migration-policy gate for the requested fully automatic
recovery of existing profiles: the old record has no evidence to choose safely.
A one-time Resume confirmation can resolve it without touching grants, but is not
silently applied in this checkpoint.

## Automated coverage

Native tests exercise real encrypted Storage, Enrollment, Execution, SyncReader,
ProvenanceVerifier and DeliveryManager: sent and received copies, reverse-direction
sync, multiline Unicode/authorship preservation, automatic incremental catch-up,
completion/pause reopen, bounded pages, interruption/replay, durable cursors,
dedupe/live races, permission OFF, revoke/remove, tamper, wrong account/device and
stale owners. These use Windows host identities, not Android physical identities.

Packaged paired Node workers exercise zero-friend scoped opaque relay, mutual
Schannel TLS, archive catalog synchronization and protected attachment transfer.
Android JVM tests exercise platform adapters; ARM64 compilation/audits do not
constitute Windows-to-Android physical interoperability acceptance.

## Unresolved physical authorization diagnosis

## Build evidence

Full Windows build/import checks, Development updater/version/extraction audit,
Android JVM/debug/test/release/lint, JNI/ARM64/dependency checks, packaged archive
boundary and existing Settings/media-performance regressions passed. Diff check
passed. Device remained unchanged; no replacement install was performed.

Windows: build/development/4.0.2-dev.80/Client/PeerChat.Desktop.exe

Android: build/development/4.0.2-dev.80/Android/PeerChat-4.0.2-dev.80-debug.apk

APK SHA-256: BBC57965ACB002016EE9FE360C973E499C1D8B3F50CA07A37781898122837404

Updater: build/development/4.0.2-dev.80/DevelopmentUpdater/PeerChat-4.0.2-dev.80.zip

ZIP SHA-256: a2b946278e9d597f8f8a142a938e6664950fa79f54c120e5fe82e1f3f437a49e

## Physical authorization evidence still required

The reported remote Not authorized state cannot be attributed conclusively from
the supplied UI description alone. Native Enrollment::devices derives access
from verified, eligible grants under locally approved authorizers, never nickname,
platform or availability metadata. The exact account/device IDs, registered grant
generations, local trusted-authorizer policy and pending publication state for
that physical run are not supplied. Do not infer missing grants or re-enroll.
The above defects are proven independently, not claimed as that run's exact cause.

## Milestone 2 security prerequisite / blocker

Existing group data cannot safely be passed through the DM archive path:

- group_message_envelope.cpp MessageLayer::receive explicitly rejects historical
  roster/key generations and requires the exact current local device in the
  roster/ledger. Archive attestation must not bypass these checks.
- group_key_manager.hpp GroupKeyManager::apply enforces one device per account;
  matchesSignedLedger binds each account to its ledger device/public key.
- The server's current group_roster_devices is mutable current membership, not
  authenticated historical epoch membership evidence. Removal deletes that row.
- Android's portable target includes group.cpp for identity/ledger utilities,
  but no group_message_envelope.cpp/group-key owner or group application receive
  path. Current Android application snapshots/commands do not expose groups.
- Archive manifests enumerate messages/message_archive_index, not protected
  group_message_index. OriginalRecord/ProvenanceVerifier is DM-only.

Implementing the requested second-device group catch-up requires a coherent
portable group application/verification boundary and authenticated epoch-specific
account/device history admission, with original signature verification and a
durable membership/revocation policy. Simply exporting stored group plaintext,
trusting source attestation as an original signature, or accepting stale envelopes
would violate the requested security constraints. No such shortcut was added.

This checkpoint is bounded reliability progress, NOT completion of Milestone 1
or Milestone 2. Group history was not implemented. No merge or publication.

## Physical checklist after outstanding prerequisites

1. Inspect exact existing grants/authorizer trust on Windows and Android without
   replacing them. Distinguish stale publication, trust and account/device scope.
2. Verify both sent and received DM history in both directions; preserve author.
3. Verify reconnect/automatic incremental catch-up without Refresh and no duplicates.
4. Verify explicit Pause and automatic OFF stay stopped; Sync now upgrades a legacy
   stopped intent only when requested.
5. Group multi-author/restart/epoch-removal tests remain blocked on the group
   security prerequisite above. Do not claim those passed.
