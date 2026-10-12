# Android direct-message pictures and files — dev.107

The verified group root checkpoint remains dev.106 (cefff6a). Production
additional-device group enrollment remains disabled. This checkpoint follows
the approved Android multimedia priority.

## Application behavior

The conversation plus button opens Android's document picker for pictures or
general files. Selected content is copied in bounded chunks into the existing
profile-private no-backup attachment directory, synced, then offered through
the profile's ONE native TransferManager. No persisted URI permission or open
picker is needed after process restart.

Authenticated incoming offers show Accept / Decline. Accepted transfers show
native progress; completion comes from verified SHA-256 and the existing
transfer completion acknowledgement. Completed pictures get bounded inline
thumbnails and an expanded picture viewer. Save as copies the original bytes
through Android's document provider. General files use the same protocol and
explicit acceptance. This does not add a mailbox for file contents.

Original names are made safe for both Windows and Android, preserving Unicode.
Files are limited by the existing 256 MiB transfer limit, four active transfers,
64 transfer journal records and the existing private-cache quota. The current
conversation projection uses those journal records; indefinite catalog-backed
attachment pagination is still follow-up work.

## Ownership and security

ReliableEndpoint advertises the existing file capability only when its platform
execution callback is present. File frames go to TransferManager after mutual
TLS, Hello, authenticated friendship and winning-session admission. File output
uses only the winning SessionTransportManager route. Android adds bounded copied
JNI commands and platform file/preview operations, no transfer engine or retry
owner. DeliveryManager text/outbox/ACK ownership is unchanged.

TransferManager's existing tick remains compatible; tickScoped adds per-transfer
route selection so archive and DM transfers to the same peer cannot choose each
other's transport. Raw friend packets cannot operate an archive transfer ID.
History Sync enrollment is not a prerequisite for DM transfer execution.

The protected transfer journal, validated source/partial identity tokens, no-
overwrite commit and completed attachment catalog are reused. Pictures decode
bounds first and are sampled for bounded allocations; viewing/export never
modifies the original. No identity, key, permission, grant or history is reset.

## Automated evidence

- Android: 124 JVM/Compose tests, zero failures; debug/test/unsigned release APKs
  and lint pass. New tests cover explicit receive consent, completion gating,
  portable Unicode names, bounded snapshot validation and offline projection.
- Added physical instrumentation test for 16 native adapter/profile reopens,
  stable directory, invalid handles, oversized commands and unauthenticated
  offer rejection. Compiled into the test APK; device execution is pending.
- Shared endpoint: 102 real Schannel/shared-runtime relay lifecycles plus
  bidirectional binary picture/file transfers and completion acknowledgements.
- Transfer manager/restart: exact transfer route isolation, Unicode/binary/empty
  files, duplicate/reconnect, SHA tamper, disk failure, cancellation, expiry,
  source changes, retained prefixes, no-overwrite and protected-journal rejection.
- Actual paired Windows Node workers: encrypted file transfer with text ACK
  coexistence, reconnect and profile close/reopen resume.
- Archive attachment catalog and paired application boundary regressions pass.
- Portable Profile/Storage/CNG/DPAPI/history/outbox/ACK/dedupe/registry and group
  device approval/revocation/envelope regressions pass.
- Full Windows build/import audit and dev.106 -> dev.107 version/updater safe
  extraction/runtime hash audit pass. Android ARM64/JNI/dependency audits pass,
  including production TransferBridge export and absence of release test hooks.
- git diff --check passes.

## Unpublished physical-test artifacts

Windows: build/development/4.0.2-dev.107/DevelopmentUpdater/PeerChat-4.0.2-dev.107.zip

SHA-256: 339ef925031dd0291fdba6a27b89ae5e1f5e9d100bc126fce7fa7f714671da2b

Android: build/development/4.0.2-dev.107/Android/

- Debug: 1df0c30e5008eed8b1d3eb9da6b15cd0d03ec526bcc5c021c38b1f0f9d76c23e
- Test: 6f79393d383d64419238dcb9c7d814ecfafb4868707e66f7f7452812ffb47758
- Unsigned release: de23df9c4a52fff0ad1b5bbc577951c100057e23be5a8de19627b2104924c342

No merge, publication, deployment or replacement install performed. Unrelated
untracked ScreenMedia objects are preserved and excluded.

Physical acceptance is pending: replacement-install on the existing Motorola,
send JPEG/PNG and binary/Unicode files both directions, accept and verify exact
copies, expand pictures, cancel/decline, interrupt/reopen a transfer, then cold
launch and verify completed attachment access with original identities intact.

Next independent work: protected oversized typed messages and text/code preview,
then secure group enrollment and Windows/Android group parity.
