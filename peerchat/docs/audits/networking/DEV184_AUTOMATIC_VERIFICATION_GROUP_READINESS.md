# dev.184 — automatic verification and group readiness (integration in progress)

## Scope and checkpoint

Branch: `codex/native-unicode-emoji`.
Worktree: `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`.
Starting checkpoint: `a10917d` (dev.183 report).
Production candidate checkpoint: `4f64e11`.

This is **not physical acceptance or a completed live integration matrix**.
The user subsequently authorized isolated real-server tests using the disposable
`codex` and `codex1` accounts. Credentials are supplied in memory, never in this
report, test sources, diagnostic exports or Git. No personal account, phone,
server installation, server configuration, key, or historical record was reset.

## Implemented changes

- `51b1e92`: Windows starts the existing scoped device-verification operation
  during signed-in capability refresh rather than waiting for an attachment
  click and modal confirmation. Existing credential resumes do not replace
  credentials. Generation, account, profile, device and owner checks remain;
  corrupt/rejected credentials fail closed.
- `30fe629`: Android explicit login always completes existing signed enrollment
  and resume, including when Remember Me is off. Remember Me controls persistence,
  not whether this session proves possession of its existing device key. No new
  authentication protocol, general signing interface or key export.
- `c418544`: missing group-room snapshots retry after the bounded pending request
  expires, rather than waiting for the healthy 60-second idle refresh cadence.
  Healthy cadence and native membership authorization remain unchanged.
- `4f64e11`: route-demand polling reads an index-only scheduling hint instead of
  decrypting up to 1,000 queued messages every second. Actual send authorization,
  protected-message validation and ACK processing are unchanged. Group transport
  scope checks decrypt the exact contact rather than every contact. They remain
  fresh reads, not a trust cache. Bounded aggregate native wait/work counters
  distinguish request, route-demand and group-demand costs.

## Evidence and limits

The physical Android report recorded gate waits up to 57.8 seconds, native relay
work up to 34.1 seconds, contact application up to 29.5 seconds, and large queued
metadata operations. These are local measurements; metadata lane durations are
not pure Pi RPC latency. The expensive code paths above were demonstrated, but
their individual contribution to every physical delay is not yet quantified.

Counting-protector regressions demonstrate route-demand decryption reduced from
at least 128 records to zero for the fixture, and exact-contact lookup from 100
decryptions to one. Those are operation counts, **not physical latency claims**.
Initial login/bootstrap still contains serialized work. Large history projections
can still be expensive. No claim is made that all lock waits are eliminated.

## Completed validation

- 470 Android unit/UI tests: zero failures/errors; Android lint passed.
- Windows client/updater evaluation and full dev.184 build passed; package native
  DLL and dependency loading checks passed.
- ARM64 dev.184 Android build passed with the pinned genuine NetEq archive.
- Existing attachment-device fixture passed enrollment/replay, pinned native
  proof, profile-bound encrypted persistence, restart resume, unchanged messaging
  token, and revoked-credential rejection. Source-only modal/readiness guards
  passed separately; these are not physical UI tests.
- Native Android group authorization and production JNI scope tests passed.
- Route-demand and exact-contact regressions passed, including corrupt contact
  rejection and independent per-device outbox demand.
- Reliable endpoint fixture passed 102 Schannel/shared-runtime lifecycles,
  message ACK/deduplication, transfer payloads and control-priority checks.
  Its approximately 4.8 ms median local ACK is **host-fixture only**.
- Mailbox protocol security/durability tests passed including signed per-device
  ACKs, restart, eligibility, rejection, rotation and revocation.
- Production JNI host transfer proof passed 16 KiB/2 MiB payloads, authenticated
  rejection and journal restart/resume. The harness required missing group-media
  link dependencies. A later old 512-character diagnostic assertion was updated
  to verify exactly 25 bounded numeric counters in five timing domains while
  retaining the original failure-text and no-identity/payload checks. The JNI
  messaging proof then passed eight reopen/Hello/message/ACK cycles, lost-ACK
  retry/dedupe and device revocation. Host SQLite/DPAPI and Java TLS are real;
  Android POSIX storage, Keystore and media are not covered by this host fixture.

## Live disposable-account results

Test: `LiveDisposableAccountsTest`, runner `tests/test-live-disposable-accounts.ps1`.
Two real Android `MessagingBackend` instances on emulator-5580, isolated package
`com.peerchat.android.voiceintegration`, separate retained profiles, actual Pi
authentication/event channels, native encryption and durable per-device ACKs.
No fabricated trust, ACKs, group authority or server responses.

Successful run evidence:
`build/tests/live-disposable-a75ad82f67054beda50b2af587fce8b4/instrumentation.log`.

| Stage | Observed local wait |
| --- | ---: |
| Explicit signed login, first account | 9,944 ms |
| First message durable receive after send returned | 5,896 ms |
| Its device ACK after receive observation | 102 ms |
| Reverse message receive after send returned | 610 ms |
| Reverse ACK after receive observation | already available |
| Group creation projection | 302 ms |
| Group invitation projection | 203 ms |
| Joined member readiness | 3,868 ms |
| Both authenticated room snapshots | 3,367 ms |
| Encrypted group-text receipt | 307 ms |
| Presence after remembered resume returned | 919 ms |

The fixture also retained authentication through a short background grace,
quiesced networking, resumed the saved credential and observed durable history.
An earlier run measured first-message receipt at 1,525 ms, reverse at 509 ms.
These are individual stage waits, not statistical performance guarantees or
complete send-to-ACK latency. The slower first-message run remains relevant.

Initial fixture issues were corrected, not hidden: a newly created group has no
voice rooms until a client creates one, and manual network reset intentionally
suppresses automatic sign-in. The successful test uses authenticated empty room
snapshots and the normal lifecycle quiescence/resume path instead.

**Still required:** full Windows/emulator cross-platform live transfer matrix,
cached delivery, repeated process restarts,
concurrent media/message/attachment activity and physical Motorola acceptance.
Two owners in one emulator process do not prove cross-device audio or OS battery
behavior. No production package is declared ready on that evidence alone.

## Preserved candidate artifacts

Under `build/development/4.0.2-dev.184/`:

- `PeerChat-Windows-4.0.2-dev.184.zip`
  SHA-256 `02244c90d3bd0d4084b1fd8d1d1adb07b50033dc77dcd0ff84ce71488e7e686a`.
- `Android/PeerChat-Android-4.0.2-dev.184-debug.apk`
  SHA-256 `846ce6498a1171c936fc287856b45dfd51229bcdfb9c9f068bb2a14c7893bf2d`.
- `messaging-repair-package-audit.json`: source/version/signature/ZIP/dependency
  verification. These ARM64/Windows candidates are preserved while the separate
  x86-64 emulator build is used for additional integration tests.

No Pi deployment or physical phone installation occurred.

## Additional real-account evidence (candidate remains withheld)

Only the authorized disposable accounts were used. Passwords arrive in memory
over an ADB loopback channel or child stdin; no credentials belong in source,
command-line arguments to test processes, reports or diagnostic exports.

- `live-disposable-47d0711fecb845249564eabbc5247fca`: direct synthetic-file
  delivery in both directions passed byte-for-byte verification. Forward offer
  wait 911 ms / verification wait 1,116 ms; reverse 1,525 ms / 2,873 ms.
  These are sequential local stage waits for a 1,824-byte file, not throughput.
- The same run timed out after 90 seconds waiting for the cached forward offer.
  The retained source/job remains intact. Server preparation returned, but native
  audience validation rejected the operation before encryption/upload.
- `live-disposable-96be064087a44c57b2ff3674b97e9de8`: narrowed rejection to
  `prepare_recipient_identity`, not source signature, local staging, or network
  timeout. The recipient account has two known device identities. Missing pin
  versus mapping is not yet distinguished; no identity was invented, silently
  omitted, revoked or trusted to make the test pass.
- `3958b38` adds fixed local stage labels only. No wire, key, schema or validation
  change. `dm-attachment-body-29079b275f14450986ad09d1db54c01c` passes real HPKE,
  signature/scope tampering, retained-source rejection/retry, restart, partial
  upload/lost append recovery, durable completion and protected storage tests.
- Windows disposable enrollment, group creation and authenticated room control
  passed after correcting the test harness's missing native account binding.
  The initial `device_session` rejection was a harness error, not a demonstrated
  production regression.
- `live-disposable-be76b39beb7e490f95523cbcbdc3bdcc` stopped at Android group
  readiness. The initial Windows helper did not perform production peer-TLS
  group-key handoff; this is a fixture gap, not proof of the reported call bug.
  The subsequent helper correction reused the production relay/controller
  boundary. Server room occupancy alone must never be reported as audio success.
- `live-disposable-53ef6f74dd854f1db02305e0eb03f7d7`: with production Windows
  `RelayTransport` and signed-roster reconciliation, Android group readiness
  passed in 6,500 ms, room visibility in 203 ms, and group join in 773 ms. Both
  authenticated participants were observed. This is key handoff/signaling and
  local audio lifecycle evidence; bidirectional media remains a separate test.
- `live-disposable-ab61633da3594a198fa20731a4f844b1` with Windows runtime
  `windows-live-runtime-b19a5a794d424defac0868bdb7320512`: real production group
  media passed in both directions through the live authenticated service.
  Android reused its retained group key (ready immediately), displayed the room
  after 412 ms, joined in 831 ms, and reached the packet threshold after another
  2,891 ms. Its sampled counters were one connected route, 361 encoded packets,
  258 received packets and 734 mixed 10-ms blocks. Windows observed a connected
  route and received packets, with no service failure during the bounded run.
  Windows input/output volumes were zero and audio processing disabled in the
  isolated test preferences. This proves encrypted media transport/counter
  activity, not speech quality, handset audio routing, or physical acceptance.

The preserved dev.184 packages predate the additional diagnostic checkpoint.
They are candidates, not the final physical-acceptance handoff. Live cached
delivery, the broader cross-platform DM matrix and concurrent activity remain
validation gates. No live server deployment is authorized by this test run.

### Repeating isolated integration tests

Build/install only the `peerchatVoiceIntegrationTest` emulator application and
its instrumentation APK. Do not substitute the physical application package.
Use `Get-Credential codex` and `Get-Credential codex1` to supply PSCredential
objects to `tests/test-live-disposable-accounts.ps1`; provide the current trusted
server endpoint and verified certificate pin. `-InspectRetained` resumes the
existing protected test profiles without creating another transfer or group.
The normal mode creates synthetic content and a uniquely named disposable group.

`tests/test-windows-live.ps1` accepts the same PSCredential pattern and requires
an isolated, marked profile under `build/tests/windows-live-profiles`. Use
`create-group` once, then retain its identifier. The `room` command is scoped to
that group and reuses a matching named room. Pass that group to the Android
runner's `-GroupId` test. Media is a separately opt-in counter-based check;
silence packet transport is not subjective audio-quality acceptance.

Keep the protected test profiles for restart/resume. Do not delete registrations
or replace unavailable device keys to make the matrix green. Any unresolved
recipient identity remains a fail-closed cache prerequisite, not an invitation
to omit a device or weaken HPKE audience verification.

### Windows/Android DM probe: incomplete mailbox fixture boundary

`LiveWindowsDmTest` and the Windows `dm` probe use a shared random probe ID and
synthetic content only. The Android probe can explicitly verify only the exact
Windows test fingerprint supplied independently from the isolated Windows
profile, using the existing current-generation verification command. It never
approves arbitrary presented identities or replaces unrelated trust.

`live-disposable-a455a34c76b1498a88a365c6f2a0aec4` and Windows runtime
`windows-live-runtime-b71ee01ff5a544b9827c699baff5947a` demonstrated Windows to
Android durable receipt and a native ACK for that device. Windows correctly
reported unresolved account-wide selection, not aggregate delivery. Android's
26,689-ms receive wait included starting the Windows process and explicit test
fingerprint verification; it is **not message transport latency**.

The reverse probe timed out. Read-only retained-state inspection
`live-disposable-ea0b41afdbc54b6fa8ef67d4bb9057b1` showed the reply's logical
recipient was the expected Windows device, but the resolved delivery target was
the other Android test device. The Windows helper did not yet run production
mailbox registration/eligibility execution. This fixture omission must be
corrected before using it to evaluate independent-device reverse delivery.
No pending message, identity, target record or server registration was removed.

The helper now reuses the packaged production `MailboxExecution.Service` in a
single-flight background task, including actual key registration and signed
eligibility installation. Generation invalidation precedes worker cancellation;
no custom mailbox protocol or fabricated response was introduced. Compilation
passed. The live repeat stopped at `other_active_friend_present` before this
service started: `codex` has an active friendship outside the two test accounts.
The account-wide production fetch must not be treated as a test-message-only
operation. No friendship was removed to satisfy the harness. Windows evidence:
`windows-live-runtime-b23473fbeb064aa7bfe0929569225195`. The corresponding Android
wait (`live-disposable-651b57be35ad431fb789c7b978519306`) timed out because the
Windows side had correctly stopped; this is not another product delivery failure.

### Concurrent Windows/Android group text and media

`live-disposable-179637061c57419884697a1a23ec686d` and Windows runtime
`windows-live-runtime-11e5284abfe34b328d1839af3d1e8b2e` passed the production room/media
path again, now sending encrypted group text while media was flowing. Android
observed group visibility in 1,224 ms, authorized retained key in 1 ms, room
visibility in another 1,426 ms, join in 1,986 ms, and media packet threshold in
another 1,122 ms. Group-text recipient acknowledgement was observed after 511 ms;
the receive counter continued increasing. Sampled counters: one connected route,
320 encoded packets, 266 received packets, 644 mixed 10-ms blocks. Windows
recorded native authenticated message admission and accepted ACK, with one
accepted recipient and zero rejections. Its final media check reported a connected
route, received packets, and no failure. Stage waits are sequential, local,
monotonic observations, not synchronized end-to-end latency measurements.

### Remaining acceptance gate

The full cached-delivery matrix is **not passed**. The earlier recipient identity
rejection and older linked test device remain unresolved. The complete DM mailbox
probe additionally needs an isolated account-wide mailbox scope. Do not solve
either prerequisite by silently omitting a device, removing a friendship,
revoking a registration, or trusting a key without verification. Existing tests,
profiles, source files, pending messages and candidate packages are retained.
Physical packages remain candidates rather than a completed release handoff.
