# Android checkpoint and remaining work audit

Audit date:2026-10-15. **Checkpoint/documentation only; development remains stopped.**
No development agents were launched, code changed, toolchain installed, branch
merged, package rebuilt/signed, device installed, server deployed or trust reset.
The only new validation was a focused `lintDebug` run authorized for this audit.

## Repository and preserved checkpoints

- Git worktree: `C:/Users/Livel/Documents/Codex/peerchat-android-groups-voice`.
- Project: `C:/Users/Livel/Documents/Codex/peerchat-android-groups-voice/peerchat`.
- Branch: `codex/android-groups-voice`.
- Exact audited implementation HEAD: `227ce313d5b9c7d40af599c5eaa8e2451849c34f`.
- Common baseline: `e0940de`; autonomous continuation started at `d67b0e5`.
- Recovery found only modified `docs/ANDROID_GROUPS_VOICE_INTEGRATION.md` and
  untracked `docs/ANDROID_DEVELOPMENT_HANDOFF.md`. All source changes were already
  committed; both interrupted documents are preserved with this report.
- Final HEAD is the documentation commit containing this report. A commit cannot
  embed its own hash; the exact final hash/cleanliness are supplied in the final
  response and accompanying `checkpoint-state.json` after commit. No source differs
  from the exact audited implementation HEAD above.
- All three prior agents are completed/stopped; no pending agent edits were found.
- Final intended state: clean tracked/index/untracked source. Existing ignored
  builds, diagnostics, synthetic fixtures and prior packages remain on disk.

| Commit | Checkpoint |
| --- | --- |
| `d6c3a64` | Mailbox recipient-provisioning failure isolation, beyond dev.155 diagnostics. |
| `158d317` | Pinned ARM64 Opus build proof. |
| `d67b0e5` | Initial integration manifest. |
| `ededec3` | Scoped pending group key query before LIMIT; old API/schema retained. |
| `2961f94` | Native group application/scoped admission and real interoperability fixtures. |
| `516aed1` | Pinned Android media/audio adapter, including saved permission correction. |
| `03211ab` | Existing CallManager/VoiceMedia application adapter and host tests. |
| `b92a575` | WIP group/voice JNI/Kotlin commands and instrumentation fixtures. |
| `0f6a801` | WIP group backend/transport/reconnect execution. |
| `794eaca` | WIP protected group preferences and notifications. |
| `227ce31` | WIP group UI/read/draft projection. |

WIP commits preserve integration work; none asserts production or physical readiness.
The complete source inventory is retained in `ANDROID_DEVELOPMENT_HANDOFF.md`.

## Validation: verified saved evidence and focused correction

| Evidence | Actual result / boundary |
| --- | --- |
| Latest existing host XMLs |190 tests,41 suites,0 failures/errors/skips. JVM/Robolectric/Compose; no new suite run in this audit. |
| Prior combined run | `build/tests/android-session/dev157-final-validation.log`: failed after3m46s on lint,1 error/25 warnings. It is not a passing release build. |
| Saved correction | Direct RECORD_AUDIO recheck immediately before AudioRecord creation, failure cleanup, manifest permissions. Present in committed source. |
| **New focused audit check** | `build/tests/android-checkpoint-audit/lint-debug.log`: **BUILD SUCCESSFUL in40s;0 errors,25 warnings**. Final Kotlin/Java source compiled as dependencies of lint. |
| Meaning of new pass | The reported MissingPermission lint blocker is resolved. No lint suppression/baseline was added. Does not rerun tests, create APKs or prove capture/audio. |
| Integrated debug | Existing dev.157 ARM64 debug built previously; source predates final permission/presentation cleanup. |
| Release | Existing on-disk release is dev.156 unsigned. No dev.157 release APK. |
| Instrumentation | Existing fixture APK compiled, never run on ART. |
| Three-participant fixture | Real Windows Node and two Android native adapters; actual Schannel/crypto, both Unicode directions, key exchange/rotation/removal/history/dedupe/restart. Host only. |
| Other native evidence |102 reliable DM/archive endpoint lifecycles;65 group scope plans;64 stale handoffs; rejected signed metadata bounds; native/portable pending projection including continuous-send starvation. |
| Voice component evidence | Actual audio.cpp Android adapter host JNI/PCM fixtures; native CallManager tests use explicit fake media; Windows receive/group/topology lifetime fixtures. Not an Android network call. |
| Emulator | Saved acceleration probe exit1/error6: hypervisor driver absent; no AVD/device; installed image x86_64 while APK ARM64. No settings changed/new emulator investigation. |
| Physical acceptance | None. No actual Motorola messaging repair or Android voice acceptance. |

Historical logs/failed runs remain preserved. New lint success supersedes the old
unrevalidated-correction status only. No final APK source correspondence is inferred
from this check. Existing tests were not silently rerun or relabeled.

## Group messaging audit

Status applies to the complete Android capability. Where a native component is
validated, the row states its limited host scope explicitly. Integrated source is
not classified as a proven full Android workflow.

| Area | Status | Actual implementation and remaining gap |
| --- | --- | --- |
|1 Discovery/listing | **Partially implemented** | GroupExecution group_state to native authenticated snapshot, metadata to backend to Compose list. Host projection tests/compile pass. No Android+actual-server workflow evidence. |
|2 Creation/joining | **Partially implemented** | UI create -> backend -> native signed creation proof -> group_register; invite accept/decline -> existing RPC. No arbitrary public-group join flow; real create/invite/join sequence unexecuted. |
|3 Invitations/membership | **Partially implemented** | group_invite_state, invite_account/respond, roster projection and device roles wired. Native signed admission remains authority. Additional same-account devices require existing explicit evidence; Android enrollment/verification UX is incomplete. |
|4 Encryption/key handoff | **Implemented and validated (native component)** | Existing ledger/Replica/GroupKeyManager/PCGE and scoped TLS; current keys and exact device pins, no group-to-friend trust. Native fixtures pass. Android JNI/Keystore network execution unvalidated. |
|5 Encrypted send | **Partially implemented** | Real UI/backend/JNI/op5/native durable create/event group_send connected.4096 UTF-8 limit; native ciphertext retained. Host execution tests and native crypto pass; real app send unaccepted. |
|6 Encrypted receive | **Partially implemented** | Foreground group_message -> native op6 expected IDs/decrypt/durable import -> ACK -> projection. No ACK for failed native call. Real Android event/JNI path not executed. Missing/current key rejection has no proven delayed-message replay pathway. |
|7 Windows/Android interop | **Implemented and validated (host fixture)** | Three participants, unchanged Windows Node and Android native adapters, both directions. Does not exercise Android ART, Kotlin event socket or full UI workflow. |
|8 Offline queued messages | **Partially implemented** | Exact retained PCGE and bounded reconnect scans implemented/tested. New creation requires installed authenticated snapshot/current key. No guarantee of cold offline composing; existing server fanout is online-recipient-only, not durable catch-up. |
|9 Retry/ACK/duplicates | **Partially implemented** | Exact-wire native retry, durable dedupe, server/native ACK path, output-drain recheck, bounded backpressure recovery. Host tests pass. Lost key/control/message ACK and delayed-event behavior need integrated app tests; server report is not E2E Delivered. |
|10 History/restart | **Partially implemented** | Native protected history/reopen/dedupe validated;8-row paging/200 projected rows. Android reload requires current group authorization; cold-offline restoreLocalLocked currently projects DM contexts, not a persisted group listing. |
|11 Unread | **Partially implemented** | Protected read/notified sequences, late-history baseline and native per-session unread; host presenter tests pass. Counts are based on bounded projected rows, not a complete native unread aggregate. Process-death/full-backlog accuracy unaccepted. |
|12 Notifications | **Partially implemented** | GroupNotifications called after native group event; privacy/permission/channel/account-scoped link handling present. WakeJob iterates DM conversations; no equivalent durable group background catch-up/push path. Runtime/background notification behavior unaccepted. |
|13 Drafts/preferences | **Implemented and validated (host component)** | Existing protected codec, separate group-qol domain/file,128 entries, account+server scope; host protection/presenter tests pass. Keystore/process-death behavior on device unvalidated. |
|14 Revocation/epoch | **Implemented and validated (native component)** | Signed/current roster checks, exact generation, route invalidation, no automatic historical-key import or new-device trust. Host tamper/removal/rotation tests pass; real app race behavior unvalidated. |
|15 Foreground/background | **Partially implemented** | Shared leased backend and foreground event worker wired. Normal mailbox-only background lease disables livePeerWork/group refresh/submission; no persistent group media/text service was added. Lifecycle/process/network integration acceptance remains. |
|16 Authentication/mailbox | **Partially implemented; phone blocked** | Existing auth/device lease/native pins reused; validated null-registration failure isolation exists. Missing Motorola recipient key and Sammy authentication rejection remain unresolved. |

### Connection audit: what exists versus what is missing

The actual source chain exists:
`ChatsHome/ConversationScreen/DetailsScreen -> NativePresenter -> MessagingApplication
-> AndroidMessagingRuntime.Lease (delegates to owner) -> MessagingBackend ->
GroupExecution -> GroupBridge -> portable/peerchat_core.cpp -> GroupApplication`.

Outgoing payloads use existing ForegroundEvents; incoming events return through
native admission. Key control uses ReliableBridge.requests, scoped relay execution,
ServerAccounts.openRelay and native ReliableEndpoint. Metadata and protected history
return through MessagingSnapshots/projectMessaging. This is not a disconnected UI
mock, and no missing mandatory group command declaration was found in this audit.

However, the host Node fixture bypasses Android's actual TLS/Keystore/JNI/event
execution, so it cannot establish this entire chain works. Essential missing work
is integrated-runtime validation plus cold-offline group presentation, background
receive/recovery semantics, delayed/missing-key event handling and explicit device
verification/enrollment UX. The server provides no durable offline group recipient
catch-up; satisfying that broader product requirement needs review/reuse of an
existing authorized History Sync path or an agreed shared interface, not a new
Kotlin store or invented ACK/fanout protocol. Group attachments remain disabled and
are not required to validate text. Search, cosmetic sender/avatar improvements and
advanced group management are optional enhancements after the essential flow.

## Voice audit

| Area | Status | Evidence / essential remaining work |
| --- | --- | --- |
|1 ARM64 Opus | **Implemented and validated (compilation)** | Pinned Opus built for ARM64/API26. No Android runtime encode/decode proof. |
|2 Microphone | **Implemented but not production-connected** | Actual AudioRecord/native Audio adapter, bounded frames, permission recheck and cleanup. Host fake-VM/device lifecycle tests only; no call owner invokes preparation. |
|3 Playback | **Implemented but not production-connected** | AudioTrack/device-paced output adapter, bounded queues, clipping/stale/mute tests. No device speaker proof. |
|4 Opus encode/decode | **Partially implemented** | Existing VoiceMedia encoder and NetEq/WebRTC Opus decoder architecture reused; pinned codec compiles. Actual Android receive dependency absent. |
|5 Focus/permissions | **Implemented but not production-connected** | AndroidCallAudio.prepare/close, focus loss and routing, manifest declarations; no Activity permission-launcher/call integration. Lint passes; runtime grants not requested. |
|6 Lifecycle/services | **Partially implemented** | Native/audio cleanup available; no active-call lifecycle owner or microphone foreground service. Existing transfer dataSync service is not a call service. |
|7 WebRTC/ICE/TURN | **Partially implemented** | Real libdatachannel/libjuice/ICE cross-build; authenticated ICE install command exists. configureIce has no application caller obtaining/refreshing server TURN credentials. |
|8 DTLS-SRTP | **Implemented and validated (compilation)** | Existing RTC/MbedTLS/SRTP compiled; no Android secured-media E2E. |
|9 NetEq | **Blocked** | Genuine pinned Android pc_rx receive artifact unavailable; no substitute supplied. |
|10 AEC/NS/AGC | **Partially implemented** | Platform session effects separate from unavailable WebRTC APM; AEC default off, NS/AGC defaults preserved. No equivalent WebRTC strength presets or physical processing quality claim. |
|11 DM signaling | **Partially implemented** | Existing CallManager frames through additive optional endpoint callback/JNI; real native owner, host replay/glare/recovery tests. Not driven by app call actions. |
|12 Incoming/outgoing UI | **Not implemented (live calling)** | Production call icon shows native unavailable diagnostic. No real Answer/Reject/End state UI/notification. Demo screens are not live calls. |
|13 DM media transport | **Blocked / partially integrated** | Real VoiceMedia factory requires availability, verified friend and live generation-bound ICE; absent NetEq prevents start. Audio preparation and TURN application wiring also missing. |
|14 Group membership/transport | **Partially implemented (native foundation)** | Existing GroupVoiceSession/RelayMeshVoiceTopology cross-compile; Windows host lifetime tests. Android ignores room/media events; no join/leave/reconcile/signaling/participant controller. |
|15 Diagnostics | **Partially implemented** | Native capability/reason snapshot shown in app; native audio/receive diagnostics exist. Active-call/audio route/processing metrics not surfaced through a connected call workflow. |
|16 Teardown/reconnect | **Implemented and validated (host component)** | CallManager generation fencing, replay, timeout, recovery and adapter cleanup tests. Actual Android focus/network/background/process interruption integration unvalidated. |
|17 Windows interoperability | **Not implemented/validated end to end** | Uses existing call/media formats and Windows components; host fake-media tests do not prove an Android/Windows voice call. |

### NetEq blocker and reusable solution

Pinned WebRTC revision: `6cf6bcb585b31414aac02377df2bc8fec770d0e8`.
Saved `build/android-media/webrtc-gn-probe.log` contains:
`//build/config/BUILDCONFIG.gn:265: assert(host_os == "linux", "Android builds are only supported on Linux.")`.
This is a host-platform restriction in the pinned Chromium/WebRTC Android build,
not absence of Android support in NetEq itself and not an Opus/RTC failure.
Windows cannot produce that target through the saved GN configuration. No usable
Linux/WSL build environment was available. Installing one is outside this audit.

There is reusable native code, but **no turnkey completed Android NetEq build script**:

- `audio_receive/peerchat_audio_receive.cpp` already wraps genuine DefaultNetEqFactory,
  WebRTC Opus, real/device-paced clock and AudioMixer behind the existing pc_rx C ABI.
- `audio_receive/BUILD.gn` expresses those pinned dependencies, presently as a
  shared_library target. `scripts/build-audio-receive.ps1` drives a Windows GN/DLL
  build and contains Windows paths/tool assumptions; it is not a Linux Android builder.
- The header has non-Windows export guards. `voice_receive_engine.cpp` already binds
  pc_rx_* statically on Android with PEERCHAT_ANDROID_STATIC_RECEIVE; Windows loader
  behavior is preserved. No second receive engine is needed.
- `android/voice-media/CMakeLists.txt` accepts PEERCHAT_ANDROID_RECEIVE_LIBRARY and
  links that artifact/defines the static-receive gate. Supplying a random Windows
  DLL or the GN shared target as if it were a complete static archive cannot work.
- A later authorized Linux build must produce the compatible ARM64/API26 artifact
  with its full dependency closure/C++ runtime/link settings and validate existing
  pc_rx ABI, real-clock pull timing and retained Opus/NetEq tests. Pin/hashes must be
  recorded. Existing adapter source should be reused, not algorithms rewritten.

`androidVoiceAvailable()` is currently false because the static receive option has
no genuine artifact. Voice capabilities are not advertised; native start/answer
reject and VoiceMedia refuses missing receive setup. Supplying NetEq is necessary
but insufficient: connect explicit permissions/focus/audio lifetime, real server
ICE configuration and call UI/services, then real DTLS-SRTP/Windows/Android audio
acceptance before enabling calls. Do not flip a capability flag as a workaround.
Group voice follows DM acceptance with the existing authorized room contract.

## Motorola messaging repair status and physical procedure

There is one validated correction beyond diagnostics: `d6c3a64` isolates null local
mailbox recipient registration so outbound preparation/signed receipt reconciliation
can continue when independently authorized. Its injected-port host regression
passes. It **does not repair local key provisioning**, permit receive without a
key, fix an unverified peer, or prove the actual Motorola symptom is resolved.
Earlier actual native HPKE/mailbox fixtures also do not prove that phone's provider.

Known saved evidence: same account/different device identities; phone had no
registered recipient key; Sammy pairing ended in authentication failure; Windows
received its independently addressed copies. No evidence shows Windows ACKs consumed
phone envelopes. Online indicates presence, not a verified messaging route.

Physical checklist for a later authorized test (none executed in this audit):

1. Keep the original dev.155 APK and dev.156 rollback. Record current version,
   account and native public device ID from diagnostics without exporting secrets.
   Preserve app data/Android Keystore/outbox/history. Never uninstall or clear data.
2. Use dev.155 first for diagnosis or a newly validated compatible diagnostic build.
   Verify package com.peerchat.android and certificate SHA256
   `db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`.
   If installation is separately authorized, use replacement install only:
   `adb install -r "<verified-diagnostic.apk>"`. The current WIP dev.157 is not a
   final acceptance package; do not treat its newer name as proof of compiled fixes.
3. Close only the competing Windows PHONE-profile session; keep Sammy's verified
   session available. Do not change account routing or pins. Check Android device
   ID is identical before/after replacement and cold restart.
4. Capture full app Diagnostics: Messaging execution, recipient registration stage,
   native initial-pin decision, authentication generation, current device mapping
   and recent_native_row IDs. Use bounded application logcat if available; do not
   publish keys/tokens/passwords, arbitrary payloads or message content.
5. Send one uniquely labeled Sammy->Android test and one Android->Sammy test. Record
   logical native message ID, intended destination device, queue/submission state,
   durable native import and authenticated ACK/receipt on both sides. Presence alone
   is never success. Inspect authorized server recipient metadata read-only if needed.
6. Separate mailbox registration failure from peer TLS/trust rejection. If registration
   fails, retain the fixed stage/provider result; if trust rejects, retain the exact
   safe native decision and verify devices through existing explicit workflow. Do
   not overwrite historical pins or manufacture a recipient binding to pass.
7. Repeat foreground reconnect, sender/recipient offline, Wi-Fi/mobile transition
   and cold app restart; confirm exact queued logical ID/ciphertext, no duplicate
   history, correct per-device receipt and preserved Keystore identity/history.
8. Reopen the Windows profile only after the isolated two-direction case, then
   verify device-specific copies independently. Do not redirect ciphertext addressed
   to Windows to the phone, or mistake authorized History Sync for repaired delivery.

## Main Work integration review

Latest read-only committed ref: `codex/native-unicode-emoji` at
`de3b64314f0ff9b29db19bfff60fcb3acc09258d` (newer than previous724116d).
Read its TRANSFER_CACHE_INTEGRATION_MANIFEST and GROUP_ATTACHMENTS_HANDOFF_2026_10_15
through git show; no checkout/index/worktree change. It is also development-frozen.
Its implementation checkpoint is3908e76; server context/receipts96cf7aa and focused
helpers18b6082 have component validation; full Windows application harness remains
WIP with capability-probe/asynchronous-output sequencing failures. Cache stays shut.

Comparison from common e0940de:51 Main Work changed paths; **zero intersecting
changed paths** with this Android source. This is not a guarantee of semantic merge
compatibility. Main changes Node/ui_bridge/managed attachment UI and server cache;
Android changes portable core/endpoint/session/Storage/audio and Kotlin/Compose.

- Main Work adds Windows-local ui_group_attachment/ui_group_attachment_network and
  typed local RPC/history/output adapters, cache_context/cache_verified/cache_receipts
  plus key-envelope operations. It adds no portable JNI/C ABI or PCGE type change.
- Android adds local GroupBridge/VoiceBridge command records and optional scoped
  endpoint callbacks, scoped Storage overload and Android platform guards. Existing
  portable C ABI header, shared crypto bytes and old API defaults remain compatible.
- Both rely on existing signed ledgers, device identity/recipient bindings, native
  verification, current epoch and explicit same-account enrollment. Do not relax them.
- Attachments require canonical signed(group,message,object) identity and protected
  private metadata separate from per-recipient HPKE packet bytes. Never store differing
  recipient envelopes as the common PCGE message wire. Historical+current roster and
  incarnation entitlement/retained key lease must verify; envelope ACK is not verified
  file completion. Android should later consume existing TransferManager and the
  agreed application projection; no duplicate file protocol, cache or HPKE stack.
- Media remains existing CallManager frames, ICE/TURN/DTLS-SRTP, and authorized
  group_room/group_media generations; attachment cache does not establish call trust.
- Independent Android changes: mailbox isolation; scoped native group query/adapter;
  Android execution/presentation; media portability with capability disabled. WIP
  application changes still require their own validation before integration.
- Potential future conflicts: shared Node/portable ownership assumptions, Storage
  lifecycle, endpoint capability negotiation, message/history projections, source
  inclusion and package version allocation. Reinspect exact refs before integration.

Recommended later order: retain common baseline and both checkpoints; integrate
reviewed mailbox/component commits first; validate native group/session/Storage API
compatibility; finish and validate Android text independently; reconcile Main Work's
frozen attachment application/harness before exposing Android attachment projections;
keep cache/media gates closed until their own acceptance. No merge/cherry-pick is
performed or authorized by this document alone.

## Prioritized remaining work

### A — Reliable Windows <-> Android DM (essential first)

- [ ] Obtain preserved-profile Motorola mailbox/provider and authentication diagnostics
  with dev.155 or a newly validated matching diagnostic APK; reproduce each direction.
- [ ] Fix only the independently identified provisioning/session/trust defect; explicit
  verification for additional devices rather than automatic pin replacement.
- [ ] Prove native registration, verified recipient selection, durable queue/import,
  ACK/restart/reconnect and independent device copies using real Android execution.
- [ ] Build audited debug/release packages from exact source and validate data-preserving
  update/certificate; keep diagnostic and rollback candidates.

### B — Fully functional encrypted Android group text (essential)

- [ ] Execute full app + actual compatible isolated server + Windows workflow:
  create, invite, accept, roster/key handoff, both text directions,3 participants.
- [ ] Close failures exposed at Kotlin event socket/JNI/Keystore/scoped relay boundaries;
  existing native fixture is a comparison, not substitute for app execution.
- [ ] Establish explicit missing/delayed-key message recovery, cold restart/offline
  history listing and retained queue behavior without broad old-key admission.
- [ ] Define/reuse authorized offline-recipient catch-up; implement group background
  wake/notification behavior under Android lifecycle rules. Existing online fanout
  cannot be described as durable offline group delivery.
- [ ] Validate revoke/epoch/device approval races, delayed/duplicate/lost ACKs,
  account switch, read/draft protection and backlog unread accuracy.
- [ ] Re-run appropriate tests/native build and produce exact-source APKs; physical
  foreground/background/process death and update acceptance remains separate.

Optional after essentials: richer group management, avatars/search, native total
unread aggregation beyond bounded projection, notification polish. Group attachments
are a separately owned integration dependency, not a reason to duplicate text crypto.

### C — Authenticated Windows <-> Android DM voice (essential)

- [ ] Build genuine pinned pc_rx/NetEq artifact on supported Linux host using existing
  adapter; resolve static/dependency closure and validate ABI/runtime pull behavior.
- [ ] Wire server-issued TURN credential fetch/refresh into VoiceExecution.configureIce;
  preserve exact profile/discovery/network/call lifetime checks.
- [ ] Wire call actions/state and explicit microphone permission/focus preparation,
  Answer/Reject/End/mute/deafen, audio ownership and safe teardown.
- [ ] Add supported active-call lifecycle/notification/foreground-service policy;
  handle background, focus/device changes/logout without silent microphone retention.
- [ ] Validate real ARM64 Opus/NetEq, authenticated signaling and ICE/DTLS-SRTP Windows
  calls, interruptions/recovery, device routing, latency/quality and bounded resources.
  Only then enable production capability/UI for a physically accepted candidate.

### D — Android group voice after DM validation (essential)

- [ ] Bind current server-authorized room membership/participation snapshots and
  existing group_room/group_media events to GroupVoiceSession/RelayMeshVoiceTopology.
- [ ] Implement join/leave/reconcile, participant attribution, mute/deafen and native
  generation-bound signaling with stale/revoked/closed-room rejection.
- [ ] Reuse one capture/encoder and attributed NetEq sources; do not add another
  receive/transport engine or represent master volume as per-person gain.
- [ ] Validate incremental joins/leaves and2/3/4participant Windows/Android audio,
  TURN routes, CPU/bandwidth, interruptions and complete device/resource release.

## Exact resume procedure — not executed

1. Obtain explicit development-resume scope; this audit does not resume it. Keep the
   existing branch/worktree and inspect final documentation HEAD plus code227ce31.
2. Read this report, both Android historical manifests and Main Work's latest committed
   manifest/handoff. Record any newly changed shared interface before editing it.
3. Preserve existing outputs and use the existing SDK/NDK/dependency pins/certificate.
   Focused check already run: from android, `./gradlew.bat -Duser.home=C:/Users/Livel
   -PpeerchatVersion=4.0.2-dev.157 lintDebug` (PowerShell quote each property argument).
4. When source packaging is authorized, run the relevant targeted checks, then
   assembleDebug/testDebugUnitTest/assembleDebugAndroidTest/lintDebug and assembleRelease
   with a documented distinct Android version override. Verify release test hooks OFF.
   These commands are instructions only; not run by this audit except lintDebug.
5. Record exact source hash, APK version/hash/certificate and validation scope before
   distributing for separately authorized physical acceptance. Never relabel an old APK.
6. Follow priorities A/B before enabling calls; C before D. Stop for trust/protocol
   ownership conflicts; retain gates rather than bypassing existing shared owners.

## Preserved artifact inventory

Existing APKs were hash-checked only; none rebuilt or signed:

| File / version | SHA256 and correspondence |
| --- | --- |
| android/app/build/outputs/apk/debug/app-debug.apk;4.0.2-dev.157 |1259f571640e61534832e76ae33b81a7947a668fa63286bcbf7d32abfc954011;29,971,971 bytes. WIP built before final cleanup. |
| android/app/build/outputs/apk/release/app-release-unsigned.apk;4.0.2-dev.156 |3e7a21aa4f1decb0402f0f5c7c0bffa7d180f9478fa4be32532d92abf1ccb57a;12,872,911 bytes. Older unsigned release; no newer group integration implied. |
| android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk |bc644f059862c8a3e9d08ed4d12386bcc0f2d55189b4a3b8371edbc443e69268;1,145,634 bytes. Compiled only. |

Preserved copies and historical suite/log evidence:
`C:/Users/Livel/Documents/Codex/2026-10-14/peerchat-android-groups-voice-session/outputs/android-handoff-2026-10-15`.
Earlier dev.156 outputs and original dev.155 in Main Work's build folder remain
untouched. Original debug certificate matches dev.155; no on-device update proof.

New audit report copies, focused lint log/result and final Git metadata:
`C:/Users/Livel/Documents/Codex/2026-10-14/peerchat-android-groups-voice-session/outputs/android-checkpoint-audit`.
Repository docs are committed; output metadata records their exact final hash.
No remaining source work is scheduled. Wait for further instructions.
