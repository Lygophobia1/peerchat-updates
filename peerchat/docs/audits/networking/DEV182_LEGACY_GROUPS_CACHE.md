# dev.181 follow-up: legacy startup, group enrollment and DM attachment modes

## Scope and evidence boundaries

Worktree: `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`.
Branch: `codex/native-unicode-emoji`; starting checkpoint: `6dd31a5`, including
the Android voice implementation at `86ab170`. Candidate validation is in progress;
this document is not a physical acceptance or package-completion claim.

No live Pi writes, deployment, phone installation, profile reset, identity
replacement, revocation change or migration is part of this follow-up. Accepted
dev.181 artifacts remain intact. Test accounts/data are isolated fixtures.

### Physical reports received on 2026-10-18

- `PeerChat-diagnostics-20261018-074604.txt`: current Jonah snapshot at
  07:46:12Z is Offline/server unavailable, with no active control client and no
  authenticated History Sync snapshot. Its large retained logs include older
  successful connections; those are not evidence of current authentication.
- Pasted JonahTest4 export at 07:49:16Z: account 2 has a fresh authenticated
  metadata snapshot, successful mailbox fetch/receipt/eligibility requests and
  cache capability requests. The selected **contact Jonah** is Offline. This
  is not a second unauthenticated local account.
  Its 32 retained control timing samples all succeeded: 7.816–194.818 ms total,
  middle sorted sample 20.495 ms. These are client-local request durations,
  not end-to-end delivery latency or proof that every server operation is fast.
- The exports omit Jonah's original startup exception. A subsequent read-only
  probe loaded its existing protected account binding (account 3, session present)
  and used the existing client `RefreshSessionInfo` with its existing certificate
  pin and no trust callback. The Pi returned **`session_invalid`**. No login,
  presence, enrollment, save, mailbox consumption or credential mutation ran.
  The response does not distinguish expiry from every other invalid-session
  cause; do not claim expiry or device revocation without further evidence.
  The first shell-hosted probe could not load a .NET Framework type and did not
  query the server; the successful diagnostic used Windows PowerShell 5 / .NET
  Framework, not the incompatible shell host.

## Findings and repairs

### Saved Windows session startup

The prior restoration path latched `discoveryRestored` before network work and
did not clear it after a temporary transport failure. Thus a single failed
startup could leave the profile offline indefinitely. Another defect classified
typed server rejections by the generic exception message rather than its error
code. The latter now directly explains why Jonah's invalid saved session was
presented as generic server unavailability instead of a sign-in requirement.
Neither defect is evidence of corrupt legacy identities.

`App.cs` and `DiscoveryRestoreRetryPolicy` now retry only transport/timeout
failures with monotonic 2/4/8/16/30-second bounded backoff. Protocol, certificate
and authorization rejection do not trigger enrollment or trust changes.
Expired/superseded session handling preserves the account binding and history.
Profile/session/manual-login epochs prevent an old restoration from replacing a
new login or sign-out. Credential loading is inside exception handling.
Allowlisted stage/type/retry diagnostics do not log credentials.
Jonah needs an ordinary sign-in in its existing profile; this repair does not
resurrect a rejected token or silently replace the device identity.

The real-server legacy compatibility fixture verifies existing unbound legacy
sessions, owned devices, bound-device sessions, presence/events/relay, and
fail-closed mismatched/revoked devices. Read-only Pi inspection found the older
account registrations still present. Stored unexpired session counts are not
counts of currently online clients.

### Group signed enrollment transport

Android used `group_control` where the server's existing wire purpose is
`group_key`. Windows also ignored the corresponding authenticated incoming
group-key demand. A visible server roster is not a signed group descriptor or
installed epoch key, so showing a roster while waiting for enrollment was not
itself an authorization failure.

Android maps the existing internal scope to `group_key` and validates the
matching server response. Windows accepts demand hints only against the exact
current roster generation, target device, source account/device and membership.
Native admission, descriptor verification, peer TLS and key handoff remain
authoritative. Repeated hints reuse an already pending/connected exact group
scope instead of starting duplicate relays. Roster rotation still creates the
new authorized scope.

### Direct voice setup

Windows could choose an online standby connection when no eligible active
session winner existed. Android correctly rejects control messages on a
nonwinning transport. Windows now selects only the active eligible winner.
The native regression proves winner-gap/standby rejection and replacement
winner selection. This is a demonstrated implementation defect, not proof that
it explains every physical first-call failure.

### Cached DM registration/readiness

Windows could start native cached-DM capability processing before the matching
mailbox/device binding was accepted. The pump now waits for registration in the
same profile, account, session and owner. The adapter reports a correlated
failure if native reply application throws, without inventing a server result.
Native correlation remains final. Some native rejections already consume their
request and schedule a retry; do not claim all such failures previously waited
for a 30-second timeout.

The isolated Windows native/server fixture passed both cached directions,
binary/Unicode integrity, durable notices/ACKs/receipts, server/client restarts
and lost-reply recovery. It proves Windows cached receiving is implemented.
Evidence: `F:\PeerChat-isolated-cache-tests\dm-application-e6206ffab0f44fb4b8188dfd93eea28f\results.log`.

An earlier fixture run on C: failed before cache-index initialization because
that filesystem had approximately 11% free space, below the mandatory 20%
reserve. The same native fixture passed on eligible F:. No quota/reserve was
weakened; this was a test-environment rejection, not a product fix.

### Outgoing Direct/Cached preference

Windows and Android have a profile-local, protected Direct-default preference
and explicit per-send choice. Direct means the existing verified device
transport, including opaque relay, not LAN-only. It does not require cache
capability or registration. Cached retains the complete authorized device
audience, HPKE, registration, revocation and capability checks. Offline Direct
may offer Cached only after readiness/authorization checks and explicit consent.
No ambiguous published cache job is converted into a new direct job.

Incoming supported direct/cache messages remain automatic. Group attachment
behavior is unchanged; the preference is labeled DM-only. Existing native
transfer negotiation, integrity, durable ACKs and journals are reused.

## Validation ledger (in progress)

- PASS: Windows mailbox reliability, full managed desktop compilation.
- PASS: Windows attachment UI/model and protected preference regressions.
- PASS: group registration/restore policy tests, including stale owner guards.
- PASS: production-server legacy session compatibility fixture.
- PASS: group ciphertext fanout and Android native group application tests.
- PASS: native voice winner/standby selection tests.
- PASS: group message envelope Stage 1A/1B suite, three-member pending/connected
  duplicate-demand regression, key ACK, text/history and roster rotation.
- PASS: focused Android preference, presenter, readiness, compatibility and
  group-purpose unit tests; instrumentation-source compilation.
- PASS: full ARM64-configuration Android unit suite: 444 tests in 97 suites,
  zero failures/errors/skips; voice-enabled APK assembly and lint passed.
  Log: `build/dev182-android-validation-sdk.log` (3m43s).
- PASS: actual Android emulator `NativeDmCacheRegistrationTest`, 1/1, about
  11 seconds, real JNI/POSIX/Keystore, current source. This is registration and
  reopen proof, not complete Windows-to-emulator file delivery.
- UI HARNESS BLOCKED: concrete `UiSmokeTest` discovered 12 tests; all failed
  before assertions because Espresso cannot find `InputManager.getInstance()`
  on the current image. Earlier invocation of abstract `UiFlowChecks` was also
  a harness-selection failure. Neither is a passing UI test or a demonstrated
  product assertion failure.
- Full genuine Android OS / Windows cached and direct transfer coverage remains
  pending. Windows-host JNI tests must not be relabeled emulator coverage.
- PASS: full Windows validation build, including native dependency audit and
  group screen C ABI lifetime checks. Validation output retains source version
  dev.180 by design and is **not** the versioned handoff package.
- PASS: actual archive-first/mailbox receipt integration: one durable signed
  receipt only after authenticated live/mailbox receive, replay/restart dedupe,
  wrong-content/account/tamper rejection; history alone grants no ACK.
- PASS: protected DM attachment history projection (608 rows, bounded 32-row
  refresh and repeated older-page handling).
- PASS: unified diagnostics read-only ownership, export, history/current-result
  separation and profile-retained failure regressions.

## Validated source checkpoints

- `503b540`: active call transport winner and duplicate group-control route reuse.
- `39891ce`: exact Windows registration readiness before DM cache capability.
- `79ed3ad`: client startup/group readiness and protected explicit attachment
  mode selection, including original picker context and fresh fallback checks.

The file picker retains launch-time account, conversation, UI generation and
mode across normal Activity recreation. A result for a stale context is dropped
rather than relabeled for a newly selected conversation. Cached confirmation
also rechecks readiness immediately before native commit.

## Shared interface / compatibility manifest

- Native `node.cpp`: active transport selection and exact-scope demand reuse;
  no wire-format, ABI, encryption or stored-history change.
- Android `ReliableRecords`, `ServerAccounts`, `MessagingBackend`: existing
  server `group_key` purpose and attachment readiness ownership. No Pi change.
- Windows `DiscoveryClient`, `ProfileAccounts`, `App`: project existing demand
  fields, validate current roster and retry saved sessions safely.
- Windows mailbox/cache adapters: readiness and correlated error handling only.
- Android application/presenter/UI contract adds explicit outgoing mode. All
  components are packaged together; no peer protocol change.
- Local sending preference uses supplemental DPAPI storage on Windows and the
  existing protected Android preference codec with a backward-compatible field.
  Message databases, transfer journals, keys and server schemas are unchanged.

## Tradeoffs and pending acceptance

Bounded retries add a small amount of connection work while a server remains
unreachable; they are not authentication retries that bypass denial. Waiting
for the winning route can defer a call until the route is genuinely usable.
Registration gating can delay cached readiness until secure enrollment finishes,
but Direct no longer waits on that cache-only work. No throughput improvement
or first-call physical success is claimed from these fixtures.

## Preserved candidate artifacts

Source checkpoint `79ed3adf91f373e86083d115dfe3841d37c263a6`:

- Windows: `build/development/4.0.2-dev.182/PeerChat-Windows-4.0.2-dev.182.zip`,
  12,504,136 bytes, SHA-256
  `b7b13922869cb6a043eedae288a737ce7cbfd23a3f180a9f49728c0d5edae71f`.
- Android: `build/development/4.0.2-dev.182/Android/PeerChat-Android-4.0.2-dev.182-debug.apk`,
  28,349,212 bytes, SHA-256
  `68a35d4209ef54c1e6e72046fa2dbb6f0a05ccd4c0b042661fa2606e045301e7`.
- `messaging-repair-package-audit.json` beside the ZIP records exact source and
  hashes. Version, existing APK signing identity, ARM64 ABI, source ancestry,
  dependency resolution and every ZIP payload hash passed. No installation or
  deployment. The three generated compiler objects are retained under
  `object-evidence` in this candidate's build directory.

Candidate packaging is complete; cross-platform fixture work and final handoff
remain in progress. Artifact existence is not full integration acceptance.

## Physical acceptance checklist

1. Update Windows and Android without uninstalling or clearing data. Confirm
   dev.182 and existing contacts, keys, histories and device names.
2. On Jonah, sign in normally to the **existing** profile. Its saved token was
   rejected by the Pi. Confirm Jonah and JonahTest4 see each other online after
   sign-in and restart; do not recreate an account or identity.
3. Test first-attempt Windows-to-Android direct voice, then repeated hang-up and
   redial. Verify audio both ways; report setup stage if it still fails.
4. Confirm the existing Android group progresses from roster-only to verified
   signed membership/current key. Test text and group voice without resetting
   membership or generating replacement keys.
5. Send two consecutive files in each direction with Direct, then explicitly
   Cached. Check file hashes/names, final save and recipient confirmation. For
   cached delivery, disconnect the sender after upload before recipient fetch.
6. Persist each default mode, restart, override a single send, cancel/reselect
   the file picker, briefly background, and switch conversation while picking.
   A stale selection must not send to the new conversation.
7. Verify messages still work during transfers, interrupted files resume safely,
   and unsupported/offline/revoked recipients are not silently omitted or trusted.
