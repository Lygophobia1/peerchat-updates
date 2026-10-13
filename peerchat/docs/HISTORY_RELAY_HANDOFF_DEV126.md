# Android archive attempt handoff and pending Windows cancellation — dev.126

Physical Windows/Android History Sync remains FAILED / PENDING RETEST.
The normal profiles, identities, signed grants, permissions, histories and
attachments have not been reset or replaced.

## Proven failure and correction

The Pi is running dev.125 (PID 24620) on 7443. Its corrected disconnect cleanup
is active: pending ownership expires or is released on socket close, and paired
channels release both owners. At 05:35:39Z reader attempt 11 owned a pending
attachment; attempts 12 and 13 were correctly rejected. Attempt 11 subsequently
paired at 05:35:45Z and closed milliseconds later. Duplicate rejection did not
demonstrate an immortal server owner and must not be suppressed.

Android RelayRequests registers an archive attempt before rendezvous. JNI open
consumes that exact pending request, but ReliableEndpoint tried to register the
same scoped transport again. Strict duplicate registration rejected it, causing
JNI open to return zero and the newly paired socket to close. A real mutual-TLS
regression pre-registering this scheduler-owned attempt failed before the fix.

The endpoint now recognizes only an existing connecting, untrusted, unaccepted
attempt with the same epoch, complete authorized scope, grant IDs/generations,
account, devices, route and network revision. Strict registration remains unique;
RelayRequests attach remains one-shot; an active winner cannot be adopted again.
Incoming TLS role is learned from the authenticated pairing result. No new
session, retry, delivery or transfer owner was added.

Windows also retained pending platform rendezvous after native retirement because
its existing writer only started after pairing. The same writer now observes
retirement before pairing and closes the socket. Native retires its platform pipe
when the manager retires that attempt. Payload cannot be emitted before a valid
pairing response, and duplicate/winner authentication remains unchanged.

## Validation and artifacts

- Real mutual Schannel TLS: scheduler pre-registration, exact scope adoption,
  verified archive import, wrong/revoked scope and direct/group isolation pass.
- Actual packaged Node workers: zero-friend archive relay, catalog/import and
  Unicode attachment transfer through the existing TransferManager pass; pending
  pause releases its native output pipe before pairing. Five profile-worker
  restarts, existing registration identity and publication gating pass.
- 100 physical socket retirement cycles (including pre-pairing), 1,000 relay
  manager lifetimes, native grant/publication/revocation, history durable imports,
  cursors, restart/deduplication and signed server metadata/relay tests pass.
- 140 Android JVM/UI tests; debug, instrumentation, release, lint and ARM64/JNI/
  pinned-dependency containment audits pass. The older Windows-host JNI harness
  cannot compile the current Android POSIX file adapter (sys/syscall.h missing);
  no production file adapter was weakened to accommodate that harness.
- On the actual Motorola, NativeArchiveRouteTest passed five profile reopen/
  Keystore/BC TLS lifetimes through production JNI, signed test grants and actual
  RelayRequests handoff. Only namespaced test profiles/keys were created and
  cleaned; the normal profile was untouched. This is not physical history import
  acceptance.
- Full Windows build, runtime import/package audit, unified version consistency,
  dev.125 -> dev.126 updater hash/extraction validation and diff check pass.

Development candidates: build/development/4.0.2-dev.126/Client and Android.
Android debug APK SHA-256:
CFDD35E00A8FA7A8534DD288A066AA4F4CEDA2E80E2090D9E7C9C5719EB52673
DevelopmentUpdaterValidated/PeerChat-4.0.2-dev.126.zip SHA-256:
d3dae61086e100e75d6042fe68544ca6c578753d949926068071bed3ee17a92c
Development unsigned policy only; no release signature or publication claimed.

The debug APK was replacement-installed, preserving normal app data. Normal
dev.126 physical testing now gets past handoff and pairs, but still reports zero
imports. At 06:09:23Z a fresh pair remained open about 21 seconds; the next pair
closed about eight seconds later. Android reports peer_tls_receive failure;
Windows reports current grants and a pending route without a data rejection.
The precise subsequent retirement boundary remains under investigation.
No physical acceptance claim. Pi dev.125 is sufficient for these client-only
changes, so it was not redeployed merely for a version increment. Existing
verified rollback backups remain preserved under /opt/peerchat-test/rollback.
