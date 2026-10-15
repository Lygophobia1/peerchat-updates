# Android groups and voice integration manifest

Current approval proposal: `ANDROID_MAILBOX_ACCOUNT_REENROLLMENT_PLAN.md`. Historical
account19→Linuxaccount4 mismatch confirmed; read-only live server metadata inspected.
Only isolated tests/docs added. No enrollment implementation, shared API, key format,
wire change or live mutation. Planned native enrollment/lease interfaces require
explicit approval and coordination; Main manifest0a7b18ce reviewed.

Latest read-only mailbox inspection: `ANDROID_REPAIR_DEV160.md`. Physical live DM
works both directions; offline recovery remains blocked by retained binding validation.
Shared coordinator/core diagnostics expand only; no wire/custody/trust change.
Main manifest96de469 rechecked. No automatic metadata repair or key recovery.

Latest focused checkpoint: `ANDROID_REPAIR_DEV159.md`, compiled code b73d035.
Shared changes: optional KeyCoordinator diagnostic callback and Android core failure
stages only. Platform facade adds beginMailboxRegistration()V; package Kotlin/JNI
together. No wire/schema/custody/trust/ACK changes. Main committed manifest reviewed
at96de469; no Main Work files modified. 202 host tests, lint/build and native online/
offline regressions pass; physical incoming still pending.

Focused Android repair resumed by user authorization after physical dev.155 evidence.
See `ANDROID_REPAIR_DEV158.md`: dev.158 reuses Windows native explicit contact approval
through a bounded Android prompt, retains registration/relay failure stages, and
fixes stale buffered DM output after revocation. Source checkpoint837c357;198 host
checks and debug/instrumentation/lint pass. Phone mailbox cause and physical delivery
remain pending. No group/voice/attachment protocol or Main Work changes.


> Finalization audit: see `ANDROID_CHECKPOINT_AND_REMAINING_WORK.md` for the
> current authoritative status. The interrupted handoff below is preserved as
> the STOP-time snapshot. Source remains227ce31; the focused authorized lint
> recheck now passes (0 errors,25 warnings), including final Kotlin compilation.
> It did not rebuild APKs or rerun the190-test suite. Main Work was re-read at
> de3b64314f0ff9b29db19bfff60fcb3acc09258d:51 changed paths, zero overlap with
> Android. Its Windows attachment application remains WIP and cache gated.


**Development stopped at the user's request on 2026-10-15.** This is a preserved
WIP integration checkpoint, not a production-ready milestone. Read
`ANDROID_DEVELOPMENT_HANDOFF.md` for exact commits, passing evidence and incomplete
validation. No further development, tests or packaging were started after STOP.

## Ownership and integration baseline

Branch `codex/android-groups-voice`; existing worktree
`C:/Users/Livel/Documents/Codex/peerchat-android-groups-voice`.
Common base: `e0940de`. Prior checkpoints: `d6c3a64` mailbox isolation,
`158d317` standalone pinned Opus cross-build, `d67b0e5` interface manifest.
Current implementation checkpoints are listed in the package evidence and Git log.
No branch/worktree was created, merged or reset during this continuation.

Before shared changes, inspected Main Work's published attachment plan/manifest at
`c8701f2`; final read-only inspection used `724116da7db84fbbaea15509a45a62c5e6f6d832`
on `codex/native-unicode-emoji`. Its 15 changed paths have no overlap with this
branch's changes against the common base. No Main Work checkout, index, commit,
profile, live Pi or physical phone was changed. No cross-chat agreement is claimed.

## Implemented group text

Android now composes the existing native GroupLedger, GroupKeyManager, Replica,
MessageLayer, Storage, DeliveryManager and SessionTransportManager. Kotlin owns
platform execution and immutable presentation only. It implements no replacement
membership database, encryption, nonce generation, trust, session or retry engine.

The live application supports authenticated group listing, creation, invitation
and accept/decline; signed roster/key synchronization; encrypted Unicode send and
receive; native sender account/device attribution; bounded persistent history;
paging, deduplication, unread state, protected drafts/read positions; explicit retry
and restart/reconnect submission of the exact retained ciphertext and logical ID.
Known contacts supply friend names; other native group sender accounts retain
unambiguous account attribution. Server reports never imply end-to-end Delivered.

Native pages contain at most8 messages; presentation retains at most200 per group.
At most4 new group previews load per pass. Group text is limited to4096 UTF-8 bytes.
Protected group preferences use the existing preference codec with a separate
`group-qol.pcr` file/domain and a128-entry bound. Existing DM preference records
remain compatible; optional `baselinePending` is omitted unless needed.

Key handoff uses existing GroupRosterEvent/GroupKeyOffer/GroupKeyAck over existing
peer TLS, authorized by current roster, epoch and exact device key. It cannot
persist ordinary friend trust. Additional same-account devices still require the
existing explicit enrollment evidence; no device or historical key is auto-trusted.
Rejected metadata is applied to a candidate ledger so rejected signed events cannot
accumulate in the live ledger. Revocation clears authority and obsolete route plans.
Key exchange completion requires this transport's exchange, durable native receipt,
a subsequent successful socket-output drain and a final native recheck.

Restart recovery scans existing protected native history, at most8 rows/4 envelopes
per pass with a239000-byte projection budget. It verifies native signature/AEAD and
protected plaintext equality without importing a message. It returns the original
PCGE; it never creates a nonce, ID, encryption or delivery state. Existing event
transport submits it. Deferred rescans preserve progress through old queued rows
while new messages arrive. Backpressure or submission exceptions reset the cursor;
server/native deduplication handles ambiguous writes.

Notifications use native-admitted history, protected read/notified cursors, the
existing privacy settings/channel and permission checks. Old history establishes
a silent baseline. Notification links require matching server/account scope.
There is no new push service. Existing group fanout is online-session-only: server
acceptance is not durable offline-recipient delivery or historical catch-up. Group
wake/push delivery and physical notification behavior remain acceptance gaps.

## Android messaging findings

The independent mailbox fix isolates null local recipient registration from
outbound preparation and signed receipt reconciliation. Receive/ACK waits for valid
local registration. Device recipient selection, cryptography, identity, durable
state and receipt verification remain native. Failed native receive cannot produce
a positive ACK or notification. No peer-authentication or historical-pin guard was
weakened. A provider failure may still prevent other native operations.

The physical Motorola's Online/Queued condition and missing mailbox registration
remain unresolved. No diagnostic evidence from that device was available here.
API/provider availability, explicit same-account device verification and native
rejection remain physical investigation gates. Do not call dev.155, dev.156 or this
candidate a proven repair. Real user keys, profiles and history were never reset.

## Voice implementation and hard gate

Actual pinned Opus, libdatachannel, libjuice, MbedTLS DTLS/SRTP and the existing
VoiceMedia build into the ARM64 native target. Android AudioRecord/AudioTrack
implements existing Audio capture/playout:16-kHz mono320-sample blocks, bounded
capture/playback queues, permission/focus preparation, mute/deafen, routing,
stale-token rejection, lifecycle cleanup and separately reported session effects.
Android AEC defaults off; NS/AGC match existing defaults. WebRTC strength presets
are unavailable through platform session effects and are not faked.

VoiceApplication borrows existing CallManager and VoiceMedia. The optional endpoint
voice callback uses existing authenticated call frames and current verified friend
pins. ICE credentials must come from the existing authenticated server and match
profile/discovery generations and expiry. Replay/glare/candidate bounds, recovery,
mute/deafen and teardown reuse native owners. An incoming media restart cannot
start capture before local Answer. Native/Kotlin capability and diagnostics are
wired into the actual app. No second call protocol or media transport was created.

**Calls remain disabled.** The actual pinned Android WebRTC NetEq receive library
is unavailable. The real GN probe fails with "Android builds are only supported on
Linux." No runnable Linux/WSL environment is installed. Production capability is
false, no voice capability is advertised, and start/answer cannot acquire audio.
A supplied artifact would require revalidation, explicit permission/focus preparation,
authenticated TURN provisioning, active-call foreground-service/lifecycle integration
and physical audio acceptance before enabling the UI. There is no microphone bypass.

Existing GroupVoiceSession, LocalVoiceSource, RelayMeshVoiceTopology and their room
contracts were inspected and cross-compiled for ARM64. Android uses the same secure
random primitive for route/SSRC allocation; Windows behavior remains unchanged.
GroupVoiceSession also fails closed without actual NetEq. Existing server
`group_room`/`group_media` formats and participation/roster/route generations are
unchanged. Unused existing room events cannot tear down Android text subscriptions.
Android room join/leave orchestration, multi-party live signaling and participant
controls are deferred behind the DM receive/acceptance gate. Per-person gain is
not represented by master volume. Group voice is not implemented end to end.
Screen/video remains unsupported. Microphone/audio-setting permissions are declared
for the gated adapter; no runtime permission request or microphone foreground
service is enabled.

## Exact shared/native touch points

| Files | Additive contract / compatibility |
| --- | --- |
| `storage.hpp`, `storage.cpp`, `delivery_manager.hpp` | Scoped pending key-handoff overload filters existing group/generation columns before LIMIT; old signatures remain. No schema/record change. |
| `group_text.hpp` | Windows-only helper/include guarded; GroupText.v1 bytes unchanged. |
| `session_transport_manager.hpp` | Current group demand/adoption/retirement/invalidation; scoped target identity; bounded plans cover128 groups x256 devices. Active/pending execution limit remains32. Existing friend/archive defaults remain. |
| `portable/relay_requests.hpp` | Optional native group-current callback, existing lease/attempt owner. |
| `portable/reliable_endpoint.hpp` | Optional final GroupAdmission/VoiceAdmission, existing constructors keep defaults. No friend pin from group scope; stale output rejected. |
| `portable/peerchat_core.cpp` | Existing profile gate owns group/voice adapters, JNI commands and existing relay servicing. C ABI header unchanged. |
| `portable/CMakeLists.txt` | Offline pinned media static dependencies and direct Audio JNI objects; release test hooks OFF. |
| `voice_apm_capture.hpp`, `voice_receive_engine.hpp/.cpp`, `voice_media.cpp`, `audio_receive/peerchat_audio_receive.h` | Android portability/static receive guards; Windows receive algorithm and pc_rx ABI unchanged. |
| `group_voice_session.cpp`, `voice_media_topology.cpp` | Android capability gate/secure-random adapter and private type rename; room/topology/media wire unchanged. |

New native adapters: `portable/group_application.hpp`, `group_pending_projection.hpp`,
`voice_application.hpp`, `android_voice_application.hpp`, `audio_android.cpp`,
`audio_android_buffer.hpp`, `voice_apm_android.hpp`, `android_media_capabilities.hpp`.
Media build inputs/probe: `android/voice-media/*`, `scripts/build-android-media.ps1`.

## Added local JNI and Android boundaries

`GroupBridge.request(handle,operation,bytes)` uses local type27/domain
`PeerChat/AndroidGroups/v1`, account and authenticated generation. Ops0 invalidate,
1 complete roster snapshot,2 metadata,3 bounded history,4 create proof,5 durable
send,6 native receive with expected group/message IDs,7 native status,8 exact retry,
9 control settled,10 final retirement,11 incoming scoped demand,12 bounded pending
projection,13 reconnect projection reset.

`VoiceBridge.request` uses local type7/domain `PeerChat/AndroidVoice/v1`, the same
account/generation binding. Ops0 snapshot,1 start,2 answer,3 decline,4 end,5 mute,
6 deafen,7 authenticated ICE. Strict snapshots report accepted/available/diagnostic
and native active call fields. Unavailable means rejected mutation/no capture.
`PeerChat/AndroidGroupRelay/v1` carries local scoped execution metadata only.
These local records are not peer/server protocol changes.

Android components changed: GroupExecution/VoiceExecution, MessagingApplication,
MessagingBackend/Snapshots, ReliableRecords/AndroidRelayExecution, ForegroundEvents,
ServerAccounts, protected preferences, notification planner/executor/WakeJob, and
ChatsHome/ConversationScreen/DetailsScreen/NativePresenter/UiState. New platform/audio
classes prepare and release Android devices. Host tests exercise injected execution
ports; instrumentation fixtures exercise actual JNI/Keystore only when run on ART.

## Protocol, crypto and Main Work dependencies

No server changes, peer protocol version, PCGE, signed ledger, HPKE/mailbox envelope,
key format, profile identity, Keystore alias, protected message schema or migration
is changed. Existing group_state/register/invite, scoped relay and event contracts
are reused. Existing device trust and enrollment restrictions remain mandatory.

Pins: Opus `ddbe48383984d56acd9e1ab6a090c54ca6b735a6`;
libdatachannel `c6696d157b5612df2a741d9a03b192b47ab6cefb`;
MbedTLS `068ff080b369adfac81509f9b57b2afabaf82dc5`;
WebRTC required `6cf6bcb585b31414aac02377df2bc8fec770d0e8`.
The existing pinned BC external-DH HPKE hook is reused, not replaced.

Main Work owns encrypted attachment descriptors, per-recipient attachment-only
HPKE envelopes, cache RPCs, journals, expiry and cleanup. No present Android text
or voice build depends on those additions. Group attachment controls stay disabled.
Future integration needs an application/history projection keyed by signed
(group,message,object) descriptor identity. Per-recipient envelopes must not be
stored as common PCGE or a single messageID envelope. Historical+current roster,
device incarnation, retained recipient binding and operation lease must all verify.
Envelope receipt is not verified file completion. Shipping cache remains gated.
No duplicate TransferManager, file codec, HPKE custody or cache server is added.

There are presently no overlapping changed paths with Main Work. Future likely
merge points are portable core/JNI, native session/Storage wiring and Android
presentation if Main Work later extends them. Reinspect its committed manifest
before merging; no automatic cherry-pick/merge is authorized by this document.

## Validated checkpoints and limits

| Evidence | Result and scope |
| --- | --- |
| Expanded native group application | Real Schannel scoped TLS, signed roster/key/ACK, Unicode PCGE, tamper, removal, reopen/dedupe, exact retry,64 stale handoffs, rejected metadata growth; synthetic host profiles. |
| Windows Node interoperability | Unchanged Windows Node and two Android native application adapters, three participants, both text directions, rotation/removal/restart/dedupe; host execution, not ART. |
| Pending projection | BCrypt and portable MbedTLS variants; protected/signature/AEAD mismatch, bounds, epochs/revocation, restart exact wire, continuous-send starvation regression. |
| Group relay |65 scope plans, stale/current retirement, actual scheduler attachment, Schannel TLS, revocation. |
| Existing reliable endpoint |102 real Schannel lifecycles, DM text/ACK/dedupe, attachments, winner/capability/logout. |
| Voice application | Explicit fake-media unit factory, real CallManager and existing wire; gate/replay/glare/64candidate bound/recovery/timeout/teardown. Not audio/network E2E. |
| Android Audio | Actual audio_android.cpp host JNI lifecycle/concurrency and PCM bounds; fake VM/device table. No physical microphone execution. |
| Windows receive/group voice/topology | Existing receive50 lifetimes, group session30 lifetimes, topology15 lifetimes; no successful live TURN/audio claim. |
| Android ARM64 | Integrated debug compiled. Latest host reports190/0 failures; combined run failed lint. Final cleanup unvalidated; no dev.157 release package. |
| Instrumentation | Actual JNI fixture APK compiled; not executed. |
| Emulator | Installed x86_64 image, no configured AVD/device; acceleration exit1/error6, hypervisor driver absent. No host settings changed; no emulator result claimed. |
| Prior mailbox suite | Existing real HPKE and isolated pinned TLS service: sender-offline delivery, per-device ACK/restart, expiry/quota, rotation/revocation/tamper. |

Initial failures are retained in logs: sandbox Windows key-provider restriction
resolved with normal provider; Android integration compiler errors corrected;
GroupExecution tests required Robolectric for Android JSONObject. None is silently
counted as a successful run. Final package evidence identifies the passing runs.

## Packaging and remaining acceptance

Existing Android-only WIP debug version `4.0.2-dev.157` is preserved; it predates
final source cleanup and is not a final validated candidate. No dev.157 release
APK was produced. The release output left on disk is the older dev.156 unsigned
APK, preserved with its correct version. No artifact was rebuilt or signed after
STOP. Original dev.155 diagnostics and dev.156 rollback outputs remain untouched.

The preserved dev.157 debug APK uses `com.peerchat.android`, versionCode2, ARM64,
minSdk26/targetSdk35. Its certificate SHA256 is
`db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`, matching dev.155.
This is the original development identity, not a distribution signing key. Matching
identity permits an update; on-device data preservation has not been executed.

Latest existing host reports contain190 tests, zero failures/errors/skips. The
combined build failed lint (1 error,25 warnings) on RECORD_AUDIO permission checking.
The direct capture permission check/cleanup and manifest declaration were saved
after that lint analysis; they were not revalidated. A later sender-name
presentation edit likewise has no final build. Instrumentation compiled but never
ran. No passing final-source release/lint result is claimed. All logs are retained.

Preserved artifact hashes, code checkpoint and final documentation HEAD are in
`outputs/android-handoff-2026-10-15/handoff-state.json` in the session output folder.
No install, deployment, key reset or destructive migration ran.

Physical gates: retained-profile update; dev.155 mailbox/auth diagnostics; DM sends
both directions with competing phone session closed; native recipient registration,
ACK/restart/network changes; Android group create/invite/roster/epoch/three devices,
Unicode/history/retry/removal; notifications and process/background behavior.
Voice additionally requires actual NetEq artifact, authenticated TURN E2E, explicit
microphone/focus permission, lifecycle/foreground-service work, headset/Bluetooth,
interruptions, device resource/battery measurements and2/3/4participant audio
acceptance before enabling DM/group call UI. Host results do not replace these gates.
