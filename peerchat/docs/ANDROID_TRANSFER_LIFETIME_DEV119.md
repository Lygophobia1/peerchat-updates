# Android explicit transfer lifetime — dev.119

Physical dev.118 feedback: the repaired images finished, approximately one minute;
locking Android still paused the download until unlock. Windows throughput was
also reported slow, but Windows did not exhibit the lock/background lifetime bug.
Android multimedia remains **FAILED/PENDING RETEST**.

## Android-only lifetime correction

Previously the final foreground lease was released on Activity backgrounding.
This deliberately quiesced peer execution; finite FCM work retained server-mailbox
work only. Thus an accepted file had no lifecycle owner keeping its existing
authenticated transfer route serviced while locked.

Explicit send/accept now starts one non-exported `dataSync` foreground service.
It leases the SAME MessagingBackend, Profile and native TransferManager. It does
not create sockets, grant trust, accept unsolicited offers, own retry, or implement
a transfer protocol. Multiple explicit transfers coalesce without extending the
30-minute service budget. A low-importance, private notification reports durable
byte progress and offers Cancel transfers through the existing native command.

The service stops on complete/cancel/failed/suspended transfers, missing projection
after a short startup grace, timeout, system timeout, severe thermal state or
provider failure. It is START_NOT_STICKY, is not boot-started, and requests no
battery/Doze exemption. One non-reference-counted partial CPU wake lock has the
same fixed timeout and is released at destruction; it is not unrestricted or
renewed. Android policy may still interrupt execution; native journals remain
authoritative for subsequent explicit recovery. Mailbox-only wake leases cannot
shorten an active explicit-transfer deadline or acquire transfer capability.

Android documentation: dataSync foreground services support user-initiated file
upload/download, require FOREGROUND_SERVICE and FOREGROUND_SERVICE_DATA_SYNC, and
Android 15 can invoke onTimeout. PeerChat stops rather than bypassing that policy.
https://developer.android.com/develop/background-work/services/fgs/service-types
https://developer.android.com/develop/background-work/services/fgs/timeout

## Restart and throughput corrections

A fully retained receiver prefix previously waited for another sender Finish
after restart before atomic completion. It now rehashes the exact full payload
and commits locally without a peer route. Corruption still fails closed. A later
sender offer receives Resume(full offset), then normal Finish/Complete exchange;
the sender's original completion-ACK validation is unchanged.

Outer Android/Windows/server relay TCP sockets now use TCP_NODELAY, matching the
existing native direct sockets. Separate small framing writes plus stop-and-wait
ACKs made Nagle/delayed ACK a plausible bottleneck, NOT a proven physical root
cause. No window, chunk, encryption, hash, flush or journal guarantees changed.
Transfer diagnostics now emit changes and bounded active stalls rather than
logging terminal rows indefinitely and overwriting the physical trace.

## Acceptance gates

135 Android JVM/UI tests passed with zero failures/errors, including lease
capability separation, transfer coalescing/budget/terminal cleanup, non-sticky
restart, system timeout and progress/Cancel notification tests. Debug/test/release
builds and lint passed. ARM64/JNI/dependency/release-hook audits passed.

Native representative 1/3/10/32 MiB transfer, corruption/atomic failure,
full-prefix offline completion and corrupted full-prefix restart rejection
passed. Actual Windows mutual-TLS/opaque-relay transfer, close/reopen recovery,
reconnect, final hash and message ACK coexistence passed. Protected attachment
catalog and portable identity/Profile/Storage/history/outbox/ACK/dedupe/registry
regressions passed. Full Windows build/package/import and transfer UI passed.

The updater preparation script is Windows PowerShell/.NET Framework tooling.
Invoking it from PowerShell 7 failed at manifest parsing; that generated candidate
was preserved. Windows PowerShell validation in DevelopmentUpdaterValidated passed
dev.118-to-dev.119 version/manifest/SHA-256/extraction/exact-runtime checks.
Unsigned Development policy only; nothing published or deployed.

Motorola Android 16/API 36/arm64-v8a replacement app/test installs passed. Five
executed NativeProfile device tests passed; one cross-process-driver-only test
was skipped (runner reports six). Includes normal identity preservation,
production protected storage, startup pragmas, isolated atomic filesystem sizes
and restart, and unauthenticated attachment rejection. Runtime fatal/linker/native
crash filter found no matching entries. Thermal status was 0 before retest.
This is not physical locked-network-transfer acceptance.

Existing Windows long-message layout timing regression remains a
known pre-existing failure; it is not hidden or weakened by this correction.

Physical test: replacement install only; send/accept multi-megabyte files, lock
screen during payload transfer, verify completion, exact hash, native ACK and
notification/service/wake-lock cleanup without unlock. Also test notification
Cancel, foreground/background transitions, disconnect/reconnect, and restart.
Record real payload timings in both directions. No synthetic result is physical
multimedia acceptance. No server deployment, merge or publication is authorized.

Artifacts under build/development/4.0.2-dev.119:

- Android/PeerChat-4.0.2-dev.119-debug.apk:
  806bd004d06979b87e8419f83e06b90788d11f4c9ccaa467ea613d000be241ab
- Android/PeerChat-4.0.2-dev.119-release-unsigned.apk:
  8569a522987315a0492354e42b0db5c89a9fe3eed02e4b1db8a11d58e31a19b4
- DevelopmentUpdaterValidated/PeerChat-4.0.2-dev.119.zip:
  f9686c8e71c6d621e6b4544b72956034eb913f81fba2752950047acfaa6620d6
