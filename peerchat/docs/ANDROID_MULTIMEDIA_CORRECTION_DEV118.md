# Android multimedia corrective candidate — 4.0.2-dev.118

Physical multimedia acceptance remains **FAILED / PENDING RETEST**. Development
packages are not publication, merge or deployment approval.

## Finalization

The production Android receiver used linkat to publish a completed cache file.
Android's untrusted-app SELinux policy forbids hard links. The connected Motorola
retains the reported incoming image as a 2,678,500-byte partial file; it was not
deleted or altered. The original generic catch did not retain errno, so the
original device errno is not claimed.

Publication now uses same-directory renameat2(RENAME_NOREPLACE), with no unsafe
overwrite fallback. File sync, rename, destination collision, partial cleanup
and directory sync have bounded stage/errno errors. A verified final inode is
retained if directory sync fails; restart reopens the recorded inode, rehashes
the committed prefix and retries durable publication before Complete/ACK.
Existing interrupted hard-link publication is accepted only for the exact owned
inode. Digest checking, journal durability and completion ACK ordering remain.

## Pacing and diagnostics

The old Android 125-ms application tick permitted only one 32-KiB preparation or
stop-and-wait payload step per tick. This introduces a pacing ceiling before RTT,
per-chunk fsync and protected journal cost. The existing relay output worker now
services the same TransferManager under the same native gate, globally bounded
to one tick per 10 ms. No second transfer, session or retry owner is introduced.
The wire chunk size, window, encryption and durable ACK behavior are unchanged.

Session-only native counters expose payload bytes, durable/acknowledged offset,
elapsed/route/first-payload timestamps, read/hash/write/flush/commit costs,
route changes, backpressure, ACK-wait ticks and aggregate journal time.
Android logs these at most once per second with a projection observation time.
They contain IDs and counters, not filenames, paths, credentials or content.
Synthetic native timing is not physical throughput evidence. TLS/network costs
and actual end-to-end UI latency must be correlated during the phone retest.

## Presentation

Ordinary incoming/outgoing text above 2,048 UTF-16 units or 12 line breaks uses
a bounded beginning preview (at most 600 units / 8 lines), View full message and
Copy. Dedicated typed-long-text cards use the same rule. Explicit file previews
remain file previews. Full text uses the existing virtualized read-only viewer;
storage, Unicode, line breaks, history and protected restart tokens are unchanged.

## Required real-device sequence

1. Replacement install only; preserve normal data, identities, keys and grants.
2. Run isolated debug native filesystem tests: 1, 3, 10 and 32 MiB, partial reopen,
   exact integrity, final reopen, repeated commit, no-overwrite collision and
   invalid token rejection. This does not substitute for network transfer tests.
3. Recover the retained incoming image through normal production restart.
4. Send and receive representative image/file sizes with Windows; correlate
   transfer IDs and payload/ACK counters, route setup, throughput and costs.
5. Interrupt a real transfer, restart and verify retained resume and final bytes.
6. Test incoming/outgoing ordinary and transferred long text, full view and copy,
   then restart/reload and confirm exact full content remains available.
7. Do not mark physical acceptance until the real bidirectional tests pass.

Android policy reference:
https://android.googlesource.com/platform/system/sepolicy/+/e6945d00463ba1c55b4cfd560757b28536f6fba1/private/app_neverallows.te

## Validation recorded

- 128 Android JVM/UI tests, zero failures/errors; debug/test/unsigned-release
  builds and lint passed. ARM64/JNI/native dependency and release-hook audits passed.
- Shared native representative sizes, corruption, typed completion failure,
  no false completion, restart, uncommitted tail, rename-crash, protected journal,
  cancellation and device binding passed.
- Actual Windows mutual-TLS/opaque-relay transfer, close/reopen resume,
  reconnect, final hash and message ACK coexistence passed.
- Protected attachment catalog, portable Storage/Profile, CNG/DPAPI identity,
  history/outbox/ACK/dedupe, registry and group-envelope regressions passed.
- Windows build/package/import and dev.117-to-dev.118 updater/version/runtime
  hash checks passed. Windows transfer UI test passed.
- Existing Windows 30-long-message layout performance assertion FAILED:
  pre-correction dev.117 measured 25.889 seconds, dev.118 22.025 seconds against
  15 seconds. Both height 6,357.6 (under the 7,800 bound). No Windows presentation
  production code changed; assertion remains intact. This is a known pre-existing
  Windows layout cost, not claimed green or a demonstrated Android throughput gain.
- Motorola moto g power (2025), Android 16/API 36, arm64-v8a, authorized ADB:
  app/test replacement installs passed. Isolated native filesystem test passed
  all representative sizes on-device in 7.033 seconds. Full NativeProfile class:
  five executed tests passed, cross-process-driver-only test skipped.
  Existing normal identity preservation, production protected storage, SQLite
  startup policy and unauthenticated attachment rejection passed.
- Original incoming partial SHA-256 remained unchanged after replacement install:
  522ca868d17426897d7d148a669a5eb974f8932f1c2663d0c705788cc48e5af6.
  It remains retained awaiting normal authenticated transfer recovery.
- No matching fatal Java/linker/SIGSEGV/SIGABRT entries found in the bounded
  post-test AndroidRuntime/libc check. Real network multimedia retest remains pending.

Artifacts under build/development/4.0.2-dev.118:

- Android/PeerChat-4.0.2-dev.118-debug.apk:
  32a09079018c8fd969a08bfcc7b3a69c167d1a783608674aa12909b60a9c01c9
- Android/PeerChat-4.0.2-dev.118-release-unsigned.apk:
  dc3827721291452d2521ba613e214e8b2b4a24d1f5380d35bfb21b75f98e824f
- DevelopmentUpdater/PeerChat-4.0.2-dev.118.zip:
  a11c632a3b2deab4706dfe39e148ffc140ab5ca0eca9d3d8e968db4c5292bf9f

No release signature, publication, deployment or physical network acceptance
is claimed. All existing group enrollment activation gates remain disabled.
