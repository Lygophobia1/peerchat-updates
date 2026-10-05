# Android headless bootstrap / bounded wake candidate

## Root causes established

Foreground and headless startup both consume the same MessagingBackend. No
MainActivity, Compose collector, selected conversation or presenter is required
to obtain authorization. The gap was in backend sequencing: refresh installed
authoritative friendship/device/presence inputs in native owners but did not
publish conversation/history snapshots. That waited for a later tick. Optional
push registration and further synchronous refresh work delayed that tick.

Periodic refresh also used a timestamp taken before blocking requests. A slow
refresh could already be overdue on return, forcing another refresh ahead of
native TLS/event/history/ACK servicing. Bootstrap performed redundant presence
requests. These were sequencing defects, not absent server roster data or a need
for a Kotlin message store.

The 50/75-second overrun was cooperative-cancellation mismatch: withTimeout could
not interrupt synchronous server socket/TLS operations or synchronized-gate
acquisition. Serial operations each had socket read/connect timeouts, but no
shared operation deadline covered the entire wake. Network write was not covered
by SO_TIMEOUT either. Physical request-stage timings now identify individual
operations, and profile open/public-key/sign operations have safe duration probes.

## Narrow changes

- Validated presence + server friendship/device snapshot -> native authorization
  -> native conversation/history projection now completes inside refresh itself.
- Native session/event/ACK servicing precedes periodic refresh. The next refresh
  interval starts after the previous refresh returns.
- Push registration no longer blocks initial conversation publication. Redundant
  bootstrap presence requests are removed; validated refresh still performs one.
- ServerChannel.OperationScope observes the active application lifetime and wake
  deadline. Its watchdog closes registered raw sockets to interrupt connect/TLS/
  read/write rather than merely cancel an outer coroutine. It has no retry policy.
- Normal requests have an additional total socket-operation timer; relay pairing
  retains its existing 20-second limit, and active long-lived sockets remain bound
  to their owning application lifetime. Certificate/pin checks are unchanged.
- The same AndroidMessagingRuntime leases coordinate UI and wake work. An active
  UI lease is not cancelled by the wake ending. Last active lease closes network
  operations immediately and queues serialized native Profile cleanup. Wake work
  records a bounded wait for quiescence; it never claims cleanup when that fails.
- Interrupted/unfinished work requests platform rescheduling. No second session,
  transport, outbox, reconnect owner, foreground service or plaintext FCM payload.
- Early success requires a real native receive event within this wake and output
  drain from that receiving route, not merely an old unread notification or an
  idle TLS handshake. This readout does not fabricate an ACK or Delivered state.

The watchdog does not forcibly abort an admitted JNI/Keystore/SQLite operation.
Those retain existing bounded-input/lifetime guards and serialized cleanup. The
real socket cancellation regression is not a claim that arbitrary OS provider or
DNS stalls can be preempted; physical duration probes remain relevant.

## Validation and evidence boundaries

- 65 Android host tests pass, including UI-independent native snapshot projection
  before delivery and rejecting historical notification/idle output as success.
- Real silent-peer TLS regression: three deadline interruptions, inner worker
  actually terminates, active sockets empty, expired scope fails closed and a new
  lifetime can reopen. No outer-Future-timeout-only assertion.
- Native JNI/Profile proof: repeated reopen/Hello/trust, history/outbox/ACK/dedupe.
- Isolated actual Server + production JNI/peer TLS: remembered device resume,
  authoritative friendship snapshot without UI, three profile/runtime cycles,
  six bidirectional deliveries, two durable offline IDs drained once.
- The Kotlin projection unit test and Java/JNI/server proof cover their respective
  production boundaries separately. They are not falsely reported as one test
  instantiating the full production Android MessagingBackend on Windows.
- FCM provider and remembered-login security regressions pass.
- Android debug/test/release builds, lint and ARM64/JNI/TLS artifact audit pass.
- Full Windows build/package audit passes at 4.0.2-dev.4, unchanged third-party
  dependencies reused. No accepted voice/screen/shared native owner changes.
- git diff --check passes. No merge or completed physical-acceptance commit.

An intermediate physical candidate restored 8 contacts / 11 known device
bindings and projected 8 conversations before relay startup. The user also
removed the app task during testing: ActivityManager explicitly reported
"remove task" for two process terminations. An older unread notification could
finish that intermediate job too early; the final stricter receive/output guard
fixes that and adds regression coverage. Do not treat the intermediate run as
accepted background delivery.

User reports unlocked/background delivery now works. Locked delivery remains an
acceptance gate; this is not general background/Doze acceptance.

## Locked wake / Windows route correlation (2026-10-03)

The Windows dev.4 snapshot at 20:29:54Z selects account 19 and the correct current
Android device. It reports verified trust but no live winning session, retrying
with backoff, and a previously closed relay. The total connected-peer count is
across conversations, not proof of a usable route to that selected device. Its
retained event section includes old device identities; those are historical
events, not evidence that the active attempt is targeting the old device.

The controlled screen-off trace at 20:29:56Z establishes:

- FCM original/effective HIGH; expedited job starts within roughly 50 ms.
- interactive=false, keyguard_locked=true, device_locked=true throughout;
  device_idle=false, thermal=0, standby bucket=10, battery_exempt=false.
- USB charging keeps Device Idle ACTIVE in this sample. Not a deep-Doze test.
- Profile opens in 138 ms; Keystore signing takes 50 ms.
- Each server request spends 3.5-3.7 s preparing its TLS context and another
  2.3-2.7 s handshaking. TCP connect and post-write response take tens of ms.
- Remember resume and server presence request succeed, but friend_state is
  interrupted before TLS completes. Contacts/projection and native relay startup
  therefore never occur. The job ends after 30.409 s, reschedule=true,
  quiescent=true, still locked. No native receive/ACK/notification in this wake.

This particular failure precedes peer-session establishment; it does not prove a
Windows stale-winner/backoff defect. Windows snapshot refresh already forwards
relay candidate reevaluation to SessionTransportManager. The isolated host
regression proves fresh remembered server sessions, presence replacement,
native relay/TLS/Hello and offline delivery, not physical locked success.

Narrow candidate correction: reuse the pinned crypto algorithm provider within
the existing OperationScope, avoiding its reconstruction per request. Each
socket still creates a fresh SSLContext with unchanged trust/pin/hostname rules;
no TLS session cache, credential cache, global provider registration, retry owner
or private-key export is added. Timings must verify the actual improvement;
the measured context cost alone does not isolate every internal library cost.
The host regression additionally warms this scope, rejects a wrong pin and an
untrusted certificate, then verifies the correct pin still succeeds.

Safe power probes run at FCM admission (after scheduling), job start/stop,
authenticated delivery, notification and completion. Safe transport probes
separate context, TCP, TLS and request-write stages; relay account/device/attempt
IDs correlate without recording server session tokens or payloads.

Replacement installs preserve identity/data. Pending: corrected candidate under
normal battery policy, single then repeated locked delivery with real Windows
ACK/Delivered evidence, notification, ordering and dedupe. No thermal override,
forced job, battery whitelist, mailbox or permanent foreground service is used.
