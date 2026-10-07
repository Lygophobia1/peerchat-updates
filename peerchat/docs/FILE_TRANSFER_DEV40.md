# Live file transfer — 4.0.2-dev.40

## Ownership / protocol

The portable TransferManager owns transfer IDs, bounded chunking, streaming
SHA-256, progress, explicit acceptance, cancellation and session-local resume.
Source/Sink/Hash are injectable; there are no sockets, reconnect timers, Windows
APIs or private keys in the manager. Its header compiles with Android ARM64 NDK.
Android does not yet advertise file support.

Windows Node supplies only its SessionTransportManager-selected, locally verified,
accepted peer-TLS winner. GroupControl-only sessions cannot carry files.
DeliveryManager and chat/mailbox Stored/Delivered semantics are unchanged.
The server carries opaque peer TLS; there is no spool, RTP use or server DB change.

Additive PT v1 capability 1024, frame type 30, file subversion 1. Frames are sent
only to capable peers; unsupported connected recipients fail clearly.
Controls: Offer, Resume/offset ACK, Chunk, Finish, Complete, Cancel.
One ID is bound to an authenticated device, basename, length and SHA-256.
Conflicting reuse is rejected. There are no new cryptographic keys.

One chunk is outstanding per transfer. Receiver contiguous offsets resume over
a freshly authenticated winning route. An ACK can cover the previous route's
last submitted chunk; the sender retains that high-water bound.
Late chunks/Finish after cancellation do not tear down messaging.
Transport failure retires the existing route through its current owner.
Complete is acknowledged only after final hash verification, flush and
no-overwrite atomic rename.

## Windows UX / file safety

Attachment opens one File transfers view. Select a verified direct conversation
then Send file. The receiver explicitly accepts and chooses a destination folder.
The save chooser selects the folder; the actual file gets a unique
<transfer ID>-<original basename> to avoid collisions. The view shows size,
progress, receiving/waiting/completed/failed/cancelled state and Cancel.
Completed received files can be revealed in Explorer; nothing is auto-executed.
Retry after failure means selecting the source again, with a new transfer ID.

Windows adapters use streaming CNG SHA-256 and Win32 file APIs. Sources deny
concurrent writers and reject directories/reparse files. Destinations reject
reparse directories and reserve partial files with CREATE_NEW.
Existing final files are never replaced. Partial names are <ID>.peerchat-part.
Reserved DOS names, traversal, separators, controls and malformed UTF-8 are
rejected; only the user chooses the destination directory.

Cleanup journals are DPAPI-protected under the profile's transfer-cleanup directory,
context PeerChat/FileTransfer/PartialCleanup/v1. They bind the exact partial path
to its Windows volume/file identity. Profile reopen authenticates the record and
validates that same file identity before deletion through an exclusive handle.
Tamper/path replacement fails closed and disables file transfers, not messaging.
Normal cancel, failure and shutdown delete owned partials and journals.
Completed files are never cleanup targets.

## Bounds / limitations

- 256 MiB/file, four active transfers, 64 retained session records.
- 32 KiB chunks, one outstanding chunk/transfer, reliable output backpressure
  at 256 KiB of the existing 1 MiB route queue.
- Fifteen-minute idle expiration; terminal replies expire too, so terminal
  offline transfers cannot demand routes forever.
- File I/O is bounded in byte count/iteration, not a hard OS disk-latency guarantee.
- Resume/dedupe are session-local. App close/restart cancels partials rather than
  durably resuming. Completed files persist, but this view is not an attachment
  history database.
- A crash between empty partial creation and cleanup-journal persistence can
  leave that empty file. No content is written before the journal is durable.
  Unknown files are deliberately not scanned/deleted.
- No offline spool, group transfer, Android transfer UI or auto-execution.

## Automated validation

Passed:

- Transfer manager: empty/small/chunk-boundary/2 MiB binary, Unicode, duplicates,
  concurrency limits, reconnect offsets, hash tamper, disk failure, cancel and
  in-flight cancel race, wrong peer, unsafe names, expiry, unsupported peer,
  atomic no-overwrite and owned partial cleanup.
- Protected journal restart, encrypted-path scan, tamper rejection and replaced
  file-identity rejection.
- Native Node/Schannel mutual TLS through opaque relay-adapter harness:
  constrained initial pinning, unverified rejection, 2 MiB Unicode binary,
  disconnect/new relay winner/re-authentication/resume, identical final bytes,
  and subsequent normal DeliveryManager message/ACK. Host, not physical Pi.
- Android NDK 27.0.12077973 ARM64 syntax compilation of transfer_portable_test.cpp.
- Actual WPF Attachment/single view/native snapshot/dark theme/minimum size/close,
  plus existing voice settings/AEC smoke.
- Portable Storage/Profile/CNG/DPAPI/history/outbox/ACK/dedupe/key registry,
  authenticated group envelope/tamper regressions.
- Shared reliable protocol: 1,000 lifecycles; Schannel endpoint: 102 real relay
  lifecycles, retry/ACK/dedupe.
- Full build.ps1 -All, package/import/restricted-load audit.
- Android: 94 JVM tests, debug/test/release builds, lint, JNI/ARM64/TLS dependency
  and no-secret/no-test-hook artifact audits.
- Version/updater extraction dev.36 -> dev.40; Development-only unsigned policy,
  exact runtime hashes. No publication.
- git diff --check.

Physical Windows-to-Windows direct/real-Pi-relay files, cancellation UX, larger
disk transfers and speaker/microphone AEC quality remain pending.
No phone is detected; no install, identity or Remember Me operation was performed.

## Artifacts / Pi

Windows: build/development/4.0.2-dev.40/Client/PeerChat.Desktop.exe
Package: build/development/4.0.2-dev.40/Package
ZIP: build/development/4.0.2-dev.40/DevelopmentUpdater/PeerChat-4.0.2-dev.40.zip
SHA-256: 52093ca460dc1b36af05e1817e122cee221032e8510d2a8f257cb43fdc6d11dd

Android debug/test/release: build/android/4.0.2-dev.40
Debug SHA-256: 505ac1e5560378b43562fcac84eccffc68f04974164d82ed5f2d4005e36ab977

No server changes were needed. Latest validated server remains dev.34,
one PID 15754, TCP 7443 confirmed listening:

- Deployment: /opt/peerchat-test/server/4.0.2-dev.34
- Config: /opt/peerchat-test/config/4.0.2-dev.34/server-config.json
- Existing DB: /opt/peerchat-test/data/4.0.2-dev.33/physical-client.db
- Certificate pin: 62A181282FE940F03F6A35541220A05F9ED2CB85A41F6DE3B0DF111286BA17E3
- coturn PID 2718 untouched; TURN/FCM configuration/secrets unchanged.

Intermediate dev.37–39 outputs were retained, not overwritten.
Dev.40 is the final file-transfer candidate. No merge or publication.
