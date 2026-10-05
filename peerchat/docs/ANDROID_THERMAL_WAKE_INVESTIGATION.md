# Motorola thermal / wake investigation — 2026-10-03

Device: authorized Motorola moto g power - 2025, Android 16 / API 36,
arm64-v8a. No thermal override, forced job execution, uninstall, data clear,
identity replacement, or protection bypass was performed.

## Physical system observations

Samples from 14:27 through 14:31 America/Chicago consistently reported
`IsStatusOverride: false` and thermal status **3 (SEVERE)**.

| Sensor | Cached service temperature / status | Live HAL temperature at final sample / status |
| --- | --- | --- |
| CPU | 50.667 C / 0 | 28.960 C / 0 |
| GPU | 50.453 C / 0 | 28.700 C / 0 |
| NPU | 50.744 C / 0 | 28.165 C / 0 |
| Skin | 45.575 C / 3 | 25.290 C / 0 |
| Battery | 31.000 C / 0 | 24.000 C / 0 |
| Power amplifier | 36.864 C / 0 | 26.021 C / 0 |
| USB port | 0.000 C / 0 | -127.000 C / 0 |

The USB-port negative reading is not a credible physical temperature; treat it
as unavailable/invalid sensor data, not cooling evidence. No separate modem/SoC
sensor is exposed in this dump; the power-amplifier reading is not a modem reading.
Battery service independently reports `temperature: 240` (24.0 C), USB powered,
100% charge, status 5 (full), AC/wireless powered false. USB connection does not
prove active battery charging at this full-charge sample.

All live HAL sensor severity flags are 0. The only severe cached sensor is Skin,
and its cached value remains unchanged while live temperatures vary normally.
This supports **possible stale/OEM thermal-service state**, not demonstrated
current overheating. No exact OEM root cause is proven. The status did not clear
over the observed four-minute interval.

## Actual scheduler and application evidence

JobScheduler history explicitly labels several PeerChat job 7341 stops `thermal`,
including runs of approximately 0.863 s, 20.001 s, 6.831 s, 13.694 s, 12.804 s,
and 27.721 s. Other runs completed their approximately 30-second application
budget; thermal interruption is therefore not a complete explanation of all
background-delivery failures. The pending job is `Restricted due to: thermal`,
not currently runnable.

Read-only physical instrumentation passed (one test). At
2026-10-03T19:31:29.184Z, PowerManager.getCurrentThermalStatus() returned **3**.
Android 16 public pending-reason APIs worked using a guarded public-API reflection
adapter (the app still compiles against API 35):

- Current pending reasons: **12 = DEVICE_STATE**.
- Bounded history included **12 (DEVICE_STATE)**, **13 (scheduler optimization)**,
  **14 (quota)** and **0 (undefined/ready)**.
- The captured historical entries have epoch timestamp 1791055578565–567 ms.

The public DEVICE_STATE category alone is not thermal-specific. It is the actual
system JobScheduler history/restriction text that supplies the thermal distinction.
Do not claim a numeric JobParameters stop reason from old builds that did not
record it.

The updated job now records UTC and monotonic start/stop timestamps, PowerManager
thermal status at each boundary, actual JobParameters.getStopReason(), and bounded
pending reasons/history. No new natural start/stop has been observed since this
instrumentation was installed; those callback measurements remain pending until
Android permits execution. Jobs are not forced to obtain that evidence.

## Bounded resilience and validation

System-interrupted wake work returns true from onStopJob so JobScheduler may
reschedule under its constraints/backoff. Explicit app/user cancellation does not
request resurrection. Replaced/stopped runs cannot call jobFinished for a later
execution. Every restart revalidates remembered authorization; no second retry
loop, socket owner, outbox, or transport is introduced.

The fixed job ID continues coalescing repeated FCM hints. Native history, ACK,
and dedupe remain authoritative and unchanged; system retry does not mark a
message Delivered. Full end-to-end background delivery/notification acceptance
is still pending and is not inferred from these system observations.

Validation: 63 Android host tests, zero failures/errors (including system-stop
retry, explicit-cancel behavior and 100-hint coalescing); debug/test/release APK
builds; lint; ARM64/JNI/TLS/dependency/test-hook artifact audit; git diff --check.
Replacement-installed debug/test APKs at 4.0.2-dev.4, preserving app data and
identity. The read-only probe did not authenticate, schedule a job or mutate
normal profile data. No Windows production source changed in this investigation.

## Post-reboot real background-message run

Reboot cleared the reported state to 0; both cached and live HAL severity flags
were 0. This strengthens the stale/OEM-state interpretation without proving the
vendor's exact fault. Interruption retry handling remains enabled.

The diagnostic APK was replacement-installed again with safe original/effective
FCM priority and transition-only owner-state counters (no message/token content).
Debug/test builds and 63 host tests passed, followed by release/lint and ARM64/JNI/
TLS artifact audit. The user reported one Windows-to-backgrounded-Android test:
no notification and sender remained Queued.

Captured America/Chicago timestamps, 2026-10-03:

- 14:50:13.127: wake job starts, PowerManager thermal=0.
- 14:50:13.157: FCM original_priority=1, effective_priority=1 (both HIGH);
  coalesces with the existing wake.
- 14:50:13.292: remembered credential loaded; shared backend lease acquired.
- 14:50:50.585 and 14:51:28.370: additional HIGH/HIGH hints coalesce.
- 14:51:28.417 and .474: relay attempts start.
- 14:51:28.485: final owner snapshot authenticated=true, presence_live=true,
  contacts=0, ready_sessions=0, history_rows=0; remembered_authentication reports
  budget_complete, then wake_job_finished.
- 14:51:48.312 and .314: relay rendezvous attempts report SocketException.

No native receive/ACK or notification-posted breadcrumb was captured. Public
pending reason at start was -1 (executing). Thermal remained 0 afterward. This
run is not explained by the previous thermal restriction or downgraded FCM.

Elapsed start-to-budget-complete was approximately 75.358 seconds, despite the
30-second coroutine budget. Inspection shows restoreRemembered uses blocking
server/native work inside withContext(IO) and synchronized(gate); coroutine
cancellation alone cannot interrupt such blocking operations. That is a proven
budget-enforcement limitation, not proof of the exact request that consumed time.
The projected contact count is not proof that the authoritative roster was empty:
refresh and publish are separate operations. Existing breadcrumbs do not yet
distinguish each resume/session-info/presence/friends/push-registration operation.
The next investigation must resolve that sub-stage and slow/blocking I/O rather
than assume thermal or invent an authentication/trust failure.

Opaque hints intentionally have no message ID, so correlation is temporal and
coalesced, not a claim that every FCM hint corresponds one-to-one with a message.
Full background delivery/ACK/notification acceptance remains failed/pending.

A second captured run independently confirms the same boundary: at 14:52:31.473
FCM is HIGH/HIGH, scheduling explicitly reports expedited=true, job starts at
14:52:31.534 with thermal=0, credential/lease at .666, and budget_complete at
14:53:22.016 (50.482 seconds). Final projected owner counters are again
authenticated/presence true, zero conversations/ready sessions/history rows.
