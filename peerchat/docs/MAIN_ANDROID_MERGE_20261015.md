# Main and Android accepted baseline integration

## Repository and rollback references

Android Work was merged into Main Work without squashing or rebasing. Both accepted working trees were clean before integration. The branches share Git history, with common ancestor `e0940deaa5094e5256568c04ac27115fb72d0fda`.

| Role | Branch | Accepted head |
| --- | --- | --- |
| Destination | `codex/native-unicode-emoji` | `446c63d0e0c2f5d0bed968d4564ccc0d1904d9e3` |
| Source | `codex/android-groups-voice` | `ac7ab34083b84276ca97b10f19ceeaaa58241896` |

Destination worktree: `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`.

Source worktree: `C:\Users\Livel\Documents\Codex\peerchat-android-groups-voice\peerchat`.

Annotated backup tags retain the exact accepted heads:

- `codex/backup/main-accepted-dev162-20261015-446c63d`
- `codex/backup/android-accepted-dev162-20261015-ac7ab34`

The merge commit containing this report has those two heads as its first and second parents. Its exact hash is included in the completion handoff. Android Work remains unchanged as a historical rollback branch and worktree.

## Integration decisions

The normal non-fast-forward merge had no textual conflicts. Review concentrated on shared transport ownership, device-bound mailbox admission, storage, native/JNI boundaries and platform-specific media code.

- Main retains its newer Windows group attachment controller, authenticated encrypted cache client/server, HPKE envelopes, clipboard pipeline and verified automatic-save workflow. The merge does not replace those files with older Android implementations.
- Android contributes its accepted group application, scoped same-key mailbox enrollment, fingerprint verification, notifications/background wake, protected preferences and media foundations.
- Shared delivery retains the existing Windows recipient path; Android scoped enrollment is an additional explicitly authorized path. Group-key pending reads gain an exact group/generation filter, without a message-history schema change.
- Group-control routes use device/group/generation continuity distinct from friend messaging and archive scopes. Real TLS tests exercise the boundaries and authenticated teardown.
- Windows audio continues using the existing DLL-backed receive implementation. Android portability/static-library guards do not enable unavailable media capabilities.
- Kotlin and native/JNI changes are built together. No new protocol, authentication engine, transfer engine or migration is introduced by integration.

The only integration-specific build change adds optional `build.ps1 -ValidationDirectory`. It runs the normal build/audits at the current source version into a new directory under this worktree's `build` folder, rejecting existing/outside destinations. Normal release allocation remains unchanged. This avoids a merge-only version bump and preserves accepted packages.

## Builds and installation identity

All validation outputs use `build/merge-validation/20261015-integration/`, except normal ignored Android Gradle outputs and isolated test evidence.

| Target | Result |
| --- | --- |
| Windows desktop, native core, ScreenMedia, server, administration and updater | `build.ps1 -All -ValidationDirectory .../Windows` passed, including packaged native dependency/ABI and restricted-load audits |
| Android ARM64, debug application and instrumentation APK | `testDebugUnitTest lintDebug assembleDebug assembleDebugAndroidTest` passed |
| Pi ARM32 | Local self-contained `linux-arm` publish of `server/portable/PeerChat.Server.csproj`, with `AttachmentCacheCandidate=true`, passed; output `PiArm32` |

Version remains `4.0.2-dev.162`. Android remains `com.peerchat.android`, version code 2, min SDK 26, target SDK 35, ARM64. APK signature verification passed and matches the accepted source APK certificate:

`db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`.

Merged validation APK: `android/app/build/outputs/apk/debug/app-debug.apk`, SHA256 `c6890faef97a71e509035d3607cbca0b082a5bdfe705bd17a5ce1955a098e008`.

The accepted ignored Android Firebase client configuration was copied into Main without changing the source file. Its hash matches the accepted configuration; generated `FCM_CONFIGURED=true` was verified. No configuration contents or credentials are committed. The accepted wake job, message service, notifications and foreground transfer service remain present.

The accepted Windows ZIP remains at `build/development/4.0.2-dev.162/PeerChat-Windows-4.0.2-dev.162.zip`, unchanged SHA256 `e00c24edbee656c88eb214927193621bcb8fbcc7a7b32cf0188818eea51433e3`. No additional release ZIP or version was allocated.

## Passing validation

- Android Gradle: 219 unit tests, zero failures/errors/skips; lint zero errors, 25 warnings. Device instrumentation tests were compiled, not run on a phone.
- Android audio buffer/JNI lifecycle, group application, voice-controller gates, explicit device verification, pending-group projection and authenticated group relay admission.
- Windows/Android group interoperability: real Schannel peer TLS, two Android native adapters, signed membership and key ACKs, Unicode messages in both directions, protected history, restart/deduplication, removal/rotation and no automatic friend trust.
- Mailbox key preparation, reconnect, reenrollment, server compatibility, scoped enrollment and scoped offline recovery. These preserve opaque key identity, original binding, wrong-account/revoked rejection, durable import, lost-ACK replay and exact receipt reconciliation. Offline spool tests are explicitly simulated transport; the separate mailbox protocol suite exercises actual pinned TLS/server storage and signed ACKs.
- Shared reliable endpoint: 102 real Schannel lifecycles, durable text/ACK/deduplication, bidirectional binary transfers, authenticated winner/capability/logout gates. Reliable peer wire regression: 1000 Hello/identity/Accepted lifecycles and malformed-input rejection.
- Production Windows group attachment application: actual native TransferManager/HPKE, managed DiscoveryClient, TLS server cache, one upload and two recipients, sender offline, server restart, client restarts around key ACK/download/finalization/receipt, revoked/missing-key rejection and safe export recovery. Evidence: `F:\PeerChat-isolated-cache-tests\application-5acdb351809c4fbbb9ceaa8c78bf7810`.
- Windows verified-download auto-save and group output tests: exact Unicode/user-selected destination, duplicate numbering/collision preservation, explicit identity-bound overwrite, restart and Retry Save. Group attachment UI and protected history/paging regression passed.
- Chat attachment rendering, clipboard PNG/DIB handling, explicit-send behavior, emoji color/favorites/multi-insertion/undo and protected Unicode history round trips passed.
- TransferManager integrity, durable resume, corrupted input, disk failure, cancellation and atomic no-overwrite regressions passed. Negotiated 32/64 KiB by 1/4/8/16 chunk tests, legacy fallback, settings and bounded ACK credit passed. Packaged in-process transfer fixture passed; its speed is not physical LAN/Pi throughput.
- Real Node transfer session passed mutual TLS/opaque relay, close/reopen and reconnect resume, final hash and message-ACK coexistence.
- History Sync: consent/enrollment, partial unresolved records, authenticated bounded pages, 67 durable imports, restart/replay, cursor/provenance protection and revocation; 12 actual peer-TLS handshakes with 25 records over four pages; archive/group scope isolation. Server signed metadata and bidirectional pinned-TLS relay tests passed duplicate rejection without winner eviction, pairing cleanup, retry and genuine session-supersession rejection.
- Windows voice receive, group voice and group screen tests passed, including independent receiver lifetimes, authenticated generation checks, 1/2/3 viewer topology, real RTP/MFT receivers, removal and teardown.

## Validation limitations

The older `test-reliable-profile-jni.ps1` Windows-host fixture cannot compile: defining `__ANDROID__` selects `portable/attachment_file_io.hpp`, which requires Linux `sys/syscall.h`. Its script, host wrapper, portable core and POSIX adapter are identical to the accepted Android head. This is a pre-existing host-fixture limitation, not an ARM64 build failure; no production security or file-I/O bypass was added to make it pass.

The first archive invocation combined mutually exclusive `-Provenance` and `-ServerMetadata` options. Its enrollment/storage/TLS/scope/provenance subtests passed before the script rejected that combination. Server authorization/metadata/relay validation was then run separately and passed. Initial Android attempts lacked the SDK environment or selected an incompatible JBR; the complete successful run used existing JDK 22 and Android SDK paths, without source/configuration changes.

`test-media-transfer-lifecycle.ps1 -RelayControl` failed before capture with "A verified screen-share capable peer is required" / "Authenticated screen offer did not reach the verified peer". The identical failure reproduced after extracting immutable accepted Main sources (`446c63d`) into an isolated build directory and running the same fixture there. No accepted worktree was changed for this comparison. The archived baseline, log (`build/merge-validation/20261015-integration/media-baseline.log`) and merged fixture evidence are retained. Concurrent screen-stop/file-transfer acceptance is therefore not established by this run; the separate real group RTP/MFT, voice and transfer tests above passed. This pre-existing fixture boundary was not expanded into a media repair milestone.

No physical merged-client media, messaging or background-wake acceptance is claimed from automated fixtures.

Android calls remain fail-closed because the pinned Android NetEq receive implementation is unavailable in this host toolchain. Existing scoped-enrollment mailbox-storage-OFF limitations and earlier independent mailbox/History Sync findings remain documented in the accepted Android reports; integration does not silently redefine their security policy or claim new fixes.

## Safety and stopping point

All runtime tests used isolated fixtures, not real user profiles. Neither live Pi services nor phone installation/data were accessed or changed. No identities, trust, grants, keys, history or server credentials were reset. No deployment, remote push, unrelated feature development or subagents were used.

The unified branch is the development baseline. Keep both backup tags and accepted packages. Stop after this merge; any new feature or deployment requires the appropriate subsequent authorization.
