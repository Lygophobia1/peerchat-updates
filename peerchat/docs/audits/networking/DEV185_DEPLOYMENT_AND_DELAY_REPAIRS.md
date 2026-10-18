# dev.185 deployment and subsequent delay repairs

## Approved deployment — 2026-10-18 UTC

Worktree: `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`.
Branch: `codex/native-unicode-emoji`. Deployment began from `1abc00d`; build-only
version override is checkpoint `fed497f`. Enrollment production logic is the
previously validated `6d6ff90` source. Later latency repairs are **not** in this
deployed package. Client packages and accepted checkpoints were not overwritten.

- Pi changed from `4.0.2-dev.180` to `4.0.2-dev.185`.
- ARM32 package: `build/linux/4.0.2-dev.185/PeerChat.Server-4.0.2-dev.185-linux-arm.zip`.
- SHA-256: `722d4de50042c17249a4a8e6450bbb7b505c113c45a562cb2aa8c1e07332d440`.
- Both existing group-cache and DM-cache compile capabilities enabled; copied
  runtime configuration remains unchanged. No cache authorization policy changed.
- Backup: `/opt/peerchat-test/rollback/pre-4.0.2-dev.185-20261018T234402Z`.
  Contains verified old runtime/configuration, private referenced files, consistent
  account DB snapshots and complete stopped cache index/ciphertext/partial backup.
- New live PID observed: `16994`; executable/configuration both dev.185, port 7443.
- Pinned TLS, existing disposable health-account login/discovery/logout passed.
  Post-start health sequence took 2,087 ms, zero retries. This is **not** a message
  delivery or pure TLS latency measurement.
- Certificate, configuration and protected account/grant contents unchanged.
  Cache mount/UUID/instance marker and free-space reserve verified; stopped cache
  SQLite integrity and backup hashes passed.
- Updater regression: 6 tests passed. ARM32 real TLS enrollment fixture passed
  scoped signatures, session binding, durable retry/restart, contact isolation
  and revocation. Isolated fixture path:
  `/opt/peerchat-test/validation/dev185-enrollment/fixtures-private`.
  Initial fixture invocations rejected a relative data path and nonprivate test
  secret permissions; rerun used an absolute isolated path and umask 077. No
  production security check was bypassed. Fixture banner uses shared dev.180
  version metadata; it compiled current enrollment source, not the old binary.

Rollback must preserve the current database and cache: stop candidate gracefully,
review durable authority/schema compatibility, then run the retained old binary
against current data only if safe. Do not restore an old DB over new user writes
or bypass the updater's revocation rollback guard.

## Initial pressure observation

Pi is ARMv7, root filesystem 117 GiB-class (103 GiB available), RAM 3,783 MiB.
Three read-only vmstat samples after deployment showed 99–100% CPU idle, zero
swap activity and zero I/O wait. This quiet sample does not measure concurrent
physical transfer pressure and cannot rule out short disk stalls.

## Confirmed authentication lock defect — local repair, not deployed

`AccountDatabase.PasswordMatches` held the shared account database gate while
performing 310,000-round PBKDF2 verification. Login and password step-up therefore
blocked unrelated account, mailbox, registration and ACK database operations.
Account creation already derives its password outside the database gate.

Repair snapshots the exact account ID/salt/hash under the gate, performs verification
outside it, then requires the exact credential record still to exist before success.
A separate password-verification gate preserves the previous single-verifier CPU
bound. No password hashing strength, identity or protocol changes.

Deterministic isolated test: injected verifier barrier 300 ms; unrelated lookup
314 ms before, under 1 ms after. These are host fixture timings, **not** physical
message latency. Wrong/missing credentials and concurrent credential replacement
rejection pass. Tests use new disposable databases only.

Server TLS lifecycle regression passed five shutdown/drain/restart cycles with
partial TLS and authenticated RPC, plus SQLite FULL/WAL/busy/reopen/corruption
checks. ARM32 isolated authentication contention and enrollment/revocation suites
also passed after this repair. Checkpoint: `b1224b0`.

## Confirmed worker-queue delay — local repair, not deployed

The retained Pi log (existing configured log directory remains dev.46) contains
worker-queue delays of 1,816–5,594 ms, including a roughly one-second growth
staircase. These samples span prior server operation, not solely dev.185.
`QueueWorker` uses the process thread pool for blocking TLS/RPC, subscriptions and
relay work. The server is concurrent, **not globally single-request**, but an idle
socket can consume a worker while newly accepted work waits for pool growth.

`DiscoveryServer.Run` now requests a minimum of 32 worker threads, retaining any
higher existing minimum and the unchanged I/O minimum. Threads are created on
demand. Failure to set the reserve warns and keeps the existing runtime behavior.
No socket, authentication, timeout, request or durability semantics changed.

Production-path isolated TLS regression: 12 partial-TLS clients plus one pinned,
authenticated discovery request; measured TCP connect through response completion.
Host baseline 4,148 ms; repaired repeat 346 ms. Repaired ARM32 Pi loopback fixture
839 ms. Both use isolated DB/certificates and exclude real phone/WAN transit.
The ARM32 run is not a before/after physical phone measurement.

Tradeoff: more workers may exist simultaneously during bursts, increasing stack
memory and scheduling overhead. This is a modest mitigation, **not** a connection
cap, unlimited-load solution or complete replacement for asynchronous I/O. Large
numbers of idle sockets can still exhaust the reserve. Preserve the separate
password-verification CPU bound rather than raising password parallelism.

## Confirmed Android mailbox retry defect — local repair, not deployed

`borrowMailboxRequest` fell back to the healthy maintenance interval after a
transport exception: 900 seconds with live events, 120 seconds without. An event
stream being connected does not prove a failed mailbox fetch or receipt recovered.

`MailboxRetryCadence` now governs the existing mailbox worker: transport failures
retry after 2, 4, 8, 16, 32, then 60 seconds (capped). Event hints cannot remove
the retry cooldown. Successful service or a changed authenticated scope resets it.
Explicit consent/reenrollment and actual foreground resume retain recovery.
TLS handshake/certificate verification failures and nontransport refusals retain
a conservative 900-second cooldown; expired sessions still use existing invalidation.
Item-level native outgoing retries remain independent. No new engine or key reset.

Tests cover failed fetch recovery without a new hint, preserved registration,
durable import followed by a lost ACK response, duplicate receive without duplicate
publication, event-hint backoff, denial and certificate failures. The scheduler
comparison (900 seconds to first retry after 2 seconds) is a deterministic policy
bound, not measured end-to-end delivery time. Repeated network outages generate
bounded additional retry traffic, with the 60-second cap limiting battery/network use.

## Confirmed cache contention — not weakened or claimed fixed

Current production admission still holds `AccountDatabase.gate` through cache disk
commit. A 250 ms injected append-commit stall blocks an unrelated mapping lookup
for 261 ms. Current direct-cache, runtime/crash and device-management-cache suites
pass, including exact retry, restart, revoked keys, unfriend/re-add, block,
cross-account denial, original recipient and completion recheck boundaries.
Tests ran on isolated F: storage; the C: fixture correctly refused insufficient
20% free-space reserve. No production volume policy was changed.

Simply unlocking append/read/progress would change ordering against revocation.
The minimal safe larger repair is a separate authorization fence acquired before
the account gate by both cache operations and **every** invalidating mutation:
friend/block changes, key/device/session revocation, group roster/device changes
and account deletion. Disk work could then release the account gate while retaining
that fence, with current-authority rechecks before publication/ACK. It needs complete
lock-order, concurrent revocation and crash tests; a cache-only patch is not safe.

## Priority and remaining investigation

1. **Mailbox transport retry:** largest demonstrated recovery-policy delay (up to
   15 minutes without a hint); repaired and tested locally.
2. **Worker queue:** retained Pi evidence of seconds before processing; reproduced
   with actual TLS and mitigated locally. Longer term, bounded async socket/RPC
   ownership is preferable to continually increasing worker minima.
3. **Password verification:** proven cross-account blockage; isolated from account
   operations while preserving credential revalidation and bounded CPU work.
4. **Cache commits:** confirmed shared-lock amplification, magnitude follows disk
   latency. Requires the broader authorization-fence work above; not unlocked.
5. **Android sign-in bootstrap:** some presence/enrollment/bootstrap operations
   still occur under the application gate. Moving them safely needs the existing
   scoped-borrow/late-result fencing pattern and real lifecycle/reconnect tests.
6. **Identity discovery:** uses independent non-RPC TLS requests, **not** the cache
   RPC lock. Up to 32 serialized snapshots per sweep may add load, but starvation
   was not reproduced; no speculative scheduling change made.

Do not infer that these results fully explain every historical 5–10 second
message delay, 25-second setup bound or 6.7-second transfer ACK. Exact current
cross-device attribution still requires correlated physical traces. No measured
phone battery, concurrent-WAN throughput or physical voice improvement is claimed.

The approved dev.185 service remains live; these subsequent repairs require a
separate versioned candidate/acceptance cycle. Do not overwrite dev.185 artifacts
or describe later local fixes as already deployed.

## Final validation and checkpoints

- `fed497f`: deployment build version override, preserving existing packages.
- `b1224b0`: password verification lock isolation and credential race regression.
- `74ea80f`: 32-worker floor and production TLS burst regression.
- `51049e0`: Android bounded mailbox retry and recovery tests.
- Full voice-enabled ARM64 Android debug build passed with pinned genuine NetEq;
  all **475 unit tests**, zero failures/errors/skips. This corrects the earlier
  attempted command's invalid version argument; the properly quoted Gradle
  properties passed. The mutable build output is validation-only, not a newly
  released dev.185 artifact, and was not installed.
- Windows legacy server compile passed with both cache features in
  `build/tests/dev185-delay-server-validation` (non-release shared version banner).
- Final ARM32 worker TLS burst and five server lifecycle/restart cycles passed.
- Existing real-TLS group-room regression passed membership denial/removal,
  reconnect, stable generations, voice/screen separation and stale signaling.
- No live personal accounts or device authorizations were changed by fixtures.

Reproduce focused checks using fresh isolated directories:

1. `dotnet run --project tests/portable/PeerChat.Server.PortabilityTests.csproj -c Release -- --auth-contention <fresh-absolute-path> fixed`
2. Same runner: `--worker-burst <fresh-absolute-path> fixed`, and
   `--lifecycle <fresh-absolute-path>`.
3. ARM32 publish that test project self-contained into an isolated output; test
   secrets on Linux require mode 0600 (run with umask 077). Do not point fixtures
   at live database/cache paths.
4. Android: `:app:assembleDebug :app:testDebugUnitTest --offline --max-workers=2`,
   with quoted `peerchatVoiceEnabled=true`, absolute pinned
   `peerchatVoiceReceiveDirectory`, and valid `peerchatVersion` properties.
5. `tests/test-group-media-rooms.ps1` for shared server group-room nonregression.

Next safe implementation boundary: review the cross-authority cache fence design
before changing commit/revocation ordering; do not apply a cache-only unlock.
For Android bootstrap, move network steps out of the application gate only through
the existing scoped operation lifetime with account/session/generation validation.
Neither unresolved area is advertised as fixed by these three checkpoints.
