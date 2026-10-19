# PeerChat dev.186 delay repairs and deployment

## Scope and checkpoints

Worktree: `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`.
Branch: `codex/native-unicode-emoji`. Starting checkpoint: `4197c9e`.
The dev.185 packages and rollback checkpoint remain intact.

This candidate combines the previous password-lock (`b1224b0`), server-worker
reserve (`74ea80f`) and Android mailbox-retry (`51049e0`) repairs with cache
authority lock isolation (`28f21c4`) and Android post-login bootstrap isolation.
No protocol, encryption, cache quota, retention, message format or transfer-window
change is included. Physical acceptance remains pending.

## Cache disk writes no longer hold the shared account lock

Previously, cache admission held `AccountDatabase.gate` through cache filesystem
and SQLite commit work. A deliberately stalled append therefore blocked unrelated
account lookups for 261 ms in the baseline fixture.

The repair introduces a separate reentrant cache-authority fence. Cache operations
validate under the account gate, release that gate for storage work, and retain
the authority fence until the operation finishes. All relevant production writers
take that fence before the account gate: device/account creation and revocation,
session and remembered-credential replacement/revocation, recipient key changes,
friendship/block changes, group roster/device changes, and account deletion.
Nested writers acquire locks in the same order. A cache-wrapper guard rejects
inverted lock acquisition. Ordinary mailbox store/fetch/ACK operations do not
acquire the new fence.

Explicit revocation cannot complete midway through an admitted cache commit.
After revocation returns, subsequent requests undergo the existing checks and are
denied. Session expiry retains the existing admission-time behavior: an already
admitted durable write may finish, but the next expired request is denied. This
does not invent an ACK for an uncommitted write. Group media keeps its original
authorization wrapper; only cache callbacks use the new wrapper.

Changed components: `server/PeerChat.Server.cs`, `EncryptedMailbox.cs`,
`AttachmentCacheAdmission.cs`, `AttachmentCacheDirect.cs`, `AttachmentCacheKeys.cs`
and `AttachmentCacheReceipts.cs`. Regression: `tests/attachment_cache_direct_test.cs`.

Measured fixture result: unrelated lookup **under 1 ms** during a 250 ms commit
barrier on both Windows host and ARM32 Pi. These are isolated injected-stall
measurements, not WAN transfer or phone ACK timings. Cache requests still serialize
with each other under `attachmentCacheGate`; registrations and explicit authority
changes may still wait for an in-flight cache operation. No unsafe cache-wide
parallelism was introduced.

## Android bootstrap releases the application lock

After signed login/resume establishes a session, contact/bootstrap network fetch
now runs outside the application gate. Installation uses the exact captured
profile, connection, session, authorization generation and foreground lease.
Concurrent restore callers share one completion rather than issuing duplicate
bootstrap requests. Stale results cannot install contacts or clear a newer login.
Background interruption retains saved credentials and schedules bounded recovery;
a transient nonremembered login requires another sign-in if interrupted.

Failure handling does not log out a stale bound session: server logout can revoke
remembered-device authority, so a late response must not invoke it against a newer
credential. Native signing remains inside checked local scope. Existing network
failure classification and revocation rejection remain fail-closed.

Changed components: Android `MessagingBackend.kt`, `ScopedBackendWork.kt`,
`ScopedBackendWorkTest.kt` and `RememberedBootstrapBackendTest.kt`.
Validated Android source checkpoint: `be99876`.

Limitation: password login and signed enrollment/resume exchanges still serialize
under the application gate. Moving that credential transaction requires explicit
operation ownership across challenge fetch, native signing, durable credential
replacement and session publication. This candidate fixes post-login bootstrap
waits, not every possible sign-in wait.

## Validation

- Windows mailbox fairness, authenticated eligibility mismatch/expiry rejection:
  passed. Windows identity adapter: passed.
- Native group voice: one-member rooms, fanout, authorization, mute/deafen,
  retired callbacks and 30 lifetimes passed.
- Real TLS group room regression: membership, signaling, reconnect, stale sessions,
  voice/screen separation and removal passed.
- Host direct cache, runtime/crash recovery and device-management cache suites:
  passed. Includes durable duplicate chunks, restart/resume, process-kill recovery,
  quota, retained original recipients, signed receipts and revocation.
- New barriers: unrelated lookup proceeds; friendship revocation waits through
  actual append commit; session, remembered-device, key, roster and managed-device
  mutators wait on the authority fence. Admission-time expiry is covered.
- Host and ARM32 authentication, worker burst, lifecycle and identity enrollment:
  passed. Pi verifier barrier lookup: 1 ms; 12-partial-TLS burst authenticated
  request: 806 ms. Loopback fixtures exclude physical Android/WAN latency.
- ARM32 direct cache suite passed on a fresh isolated Pi database/cache, including
  the new barriers. One initial shell wrapper had a quoting error after successful
  tests; a fresh final invocation returned exit code 0.
- Deployment helper: six regression tests passed.
- Windows dev.186 full build, HPKE, dependency containment, native loading and
  group voice/screen C ABI lifetime audits passed.
- Linux x64, ARM64 and self-contained ARM32 builds passed version/runtime/architecture
  and payload audits. Pinned .NET SDK 10.0.401 and runtime 10.0.12 retained.
- Final frozen Android source: 480 unit tests passed, zero failures/errors/skips;
  ARM64 voice-enabled debug assembly passed; lint passed with zero errors and
  25 warnings. Tests include stalled off-gate bootstrap, stale session/background
  results, partial installation advancing generation before failure, protected
  credential retention/retry, revoked credentials and foreground lease changes.
- Client packaging passed source-ancestry, version, APK signing identity/ABI,
  native dependency containment and ZIP payload byte/hash audits.

Host evidence: `F:\PeerChat-isolated-cache-tests\dev186-main` and
`F:\peerchat-cache-test-fixtures`. Pi isolated evidence:
`/opt/peerchat-test/validation/dev186-delay`. These fixtures do not use personal
accounts or the production message/cache databases.

## Packaging and deployment record

The Pi was updated from dev.185 to dev.186 after checkpoint `28f21c4` and host/ARM32
verification. Live PID 18001 and port 7443 were confirmed. ARM32 package SHA-256:
`bd98df9396e0fdba2d5b9fbcf1390d5ddc7352a779a31fc58ee7d917339b6d67`.

Backup: `/opt/peerchat-test/rollback/pre-4.0.2-dev.186-20261019T002037Z`.
The compatible updater verified full runtime/configuration/private-file backups,
consistent stopped database/cache backups and cache integrity. Pinned TLS,
disposable health-account authentication and discovery passed with zero startup
retries (2,107 ms for the combined health sequence, not pure connection latency).
Certificate, configuration and protected identity/grant contents were unchanged;
cache mount/marker/free-space reserve checks passed. The existing cache remains
enabled with unchanged quota/retention.

Rollback must retain current durable data and revocation authority; never restore
an older database over new user writes. The previous runtime is preserved in the
backup, and the updater does not bypass its revocation-aware rollback guard.

Client packages use source checkpoint `be99876`, version `4.0.2-dev.186`:

- Windows: `build/development/4.0.2-dev.186/PeerChat-Windows-4.0.2-dev.186.zip`
  (12,547,565 bytes), SHA-256
  `5d525ac7c29590cc4ddba66d0374b33abd7fa964a52a4d09291a6c4741b2e05e`.
- Android: `build/development/4.0.2-dev.186/Android/PeerChat-Android-4.0.2-dev.186-debug.apk`
  (28,998,918 bytes), SHA-256
  `ee2ef9e7ddd6e85d6de4b437be3e31461bb16aacae791c273871ac42cfef1e0a`.
  Existing signing identity retained; ARM64, genuine pinned NetEq enabled.
- Package audit and deployment evidence are retained in that version directory
  as `messaging-repair-package-audit.json` and `pi-deployment-report.json`.

The packaging helper now accepts an explicit `-PiDeployed` flag so the manifest
accurately records this completed deployment; its default remains false.
No phone installation was requested or performed. All earlier packages remain
available. Physical acceptance is pending; stop here rather than expanding scope.

## Physical acceptance

1. Update both clients without uninstalling or clearing data; sign in/restart and
   briefly background/return during connection setup.
2. Send messages both ways during a cached upload/download; check per-device ACKs.
3. Retry interrupted cached/direct transfers; verify completion and saved filenames.
4. Join a group voice room on both devices and check bidirectional audio.
5. Export diagnostics if delays remain, noting the failed action and time.

Remaining networking work should prioritize scoped signed-login transactions and
bounded asynchronous server socket/RPC ownership. Increasing worker minima again
or changing transfer windows is not a substitute for measured queue ownership.
The current worker reserve may use more thread-stack memory during bursts, and
bounded mailbox retry adds recovery traffic during outages. No physical battery,
voice smoothness or end-to-end latency improvement is claimed from fixtures.
