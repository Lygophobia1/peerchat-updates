# PeerChat dev163 Group History Test Report

Group text and separately typed signed public attachment-reference recovery are implemented in the unified Windows and Android codebase. Automated validation passed; physical acceptance is pending. Nothing was installed on the phone or deployed to the Pi.

## Checkpoints and compatibility

Worktree: `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`

Branch: `codex/native-unicode-emoji`

Both packages were built from **`1ebc47ae16dc5eb96728d2d80da448ed36e43301`**. Implementation checkpoint: `46e2d2d`; earlier text/evidence/projection checkpoints: `bd04009`, `11f4d57`, `d05d361`. The accepted dev.162 merge `42da2cf`, backup tag and original package remain available.

Use dev.163 on both test clients. New capability `16384` negotiates group-history lanes; released dev.162 clients do not advertise it. Existing DM, PCGE, HPKE, encrypted attachment cache and media formats remain unchanged. There is no Raspberry Pi protocol or deployment dependency.

## Implementation and security

The existing History Sync reader now serves bounded group text and public-reference pages over admitted same-account archive or group-control peer TLS. Protected CAS cursors advance after verified durable imports; completion ACKs are not transport-connectivity claims. Restart, replay deduplication, late keys and alternate authorized sources use the existing owners.

`archive_group_history.hpp` verifies current and original epoch membership instances, sender signature and scope. Text also requires its retained epoch key and original PCGE/GCM validation. Unknown historical evidence stays unresolved. Rejoining with the same identity does not restore old access, and revoked members cannot fetch unauthorized records.

`storage.cpp/.hpp` retain original PCGE rows and add a separately typed protected reference/index. Immutable ID reuse with conflicting content or a different history record type fails closed transactionally. The signed public descriptor is reused unchanged. Its existing public length fields, identifiers, epoch, ciphertext digest and signature are retained; filenames, HPKE packets, keys and private metadata are not synchronized through this lane.

`node.cpp`, the portable group owner/endpoint/core, WPF and Kotlin projections reuse the existing worker, native operations and local storage. Windows extracts public evidence from current and completed protected attachment jobs in bounded pages without starting downloads. Native revisions refresh Android histories without requiring a live message event. Original text timestamps/order and opaque older-page cursors are preserved.

Reference-only entries explicitly say private metadata and original time/order are unavailable. The public descriptor does not authenticate those fields; they are not reconstructed. References grant no download rights and do not establish cache availability. Windows retains an existing authorized attachment bubble, or upgrades the same logical bubble when independently authorized private metadata arrives. Cache expiry and eviction are unchanged.

Shared interfaces, ownership and dependencies are recorded in `GROUP_HISTORY_INTEGRATION_MANIFEST.md`.

## Validation

- Group history fixtures with Windows and portable crypto: 74 nonempty text imports, 17 public references across three bounded pages, restart/replay/deduplication, delayed epoch keys, membership rotation, historical sender signatures, new/rejoined/revoked devices, signed grant revocation, tamper, wrong account/group, conflicting record IDs and protected reopen. Group completion counts deduplicate text/reference lanes.
- Actual Windows Node and two Android native-controller adapters over real mutual TLS: seven missed Windows and five missed Android messages, public references in both directions, original-source-offline member recovery, protected restart and revocation/key rotation. This is host integration, not a Motorola test.
- Existing DM archive regression: real peer TLS, nonempty pages, provenance, purpose/device isolation, consent/enrollment, stale authorization and revocation.
- Actual attachment application, TransferManager, native HPKE, managed discovery and isolated TLS/cache server: upload and two independent recipient downloads, interrupted preparation/download/finalization/receipt, restart, integrity, epoch checks, disabled-cache handling and bounded completed-history reference extraction.
- Windows all-component build and runtime/DLL/HPKE audits passed. Final-package group attachment UI, reference upgrade/deduplication, clipboard PNG/DIB/text handling, emoji/favorites/multiple insertion/encrypted Unicode restart, and automatic verified saving regressions passed.
- Android ARM64/JNI build, **225 unit tests, zero failures/errors/skips**, and lint passed. Lint retains 25 warnings and zero errors. No device instrumentation or physical installation was performed.
- Existing transfer, group voice/screen lifetime, voice receive, scoped mailbox/enrollment and offline recovery regressions passed earlier in this focused milestone; accepted media behavior was not reworked.

Evidence is retained under ignored `build/`, including `dev163-windows-build.log`, `dev163-android-build.log`, `dev163-history-interop.log`, `dev163-archive-security.log`, `dev163-interrupted-reference-test.log`, `group-references-checkpoint-security.log`, `group-references-final-portable.log`, and `group-references-final-attachment.log`. The explicit interrupted-reference fixture reopens the existing reader between reference pages and finishes all 17 imports without keys or duplicates. Its later test-only change and this report do not change the packaged production source checkpoint.

The initial reference fallback assertion checked text completion before the reference lane had finished. The corrected fixture waits for reference completion before teardown; it does not weaken authentication. A separate invalid test-cursor/out-of-bounds assumption was removed before the passing security runs. Earlier test-launch argument/redirection errors are documented in the integration manifest.

## Manual test packages

Windows ZIP:
`C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat\build\development\4.0.2-dev.163\PeerChat-Windows-4.0.2-dev.163.zip`

Windows executable:
`C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat\build\development\4.0.2-dev.163\Package\PeerChat.Desktop.exe`

Windows ZIP SHA-256: `b00872a2576044f7bb4a392ec4d6af1965c2ab0e0d1f9bc078c476c135623ef4`

Android APK:
`C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat\build\development\4.0.2-dev.163\Android\PeerChat-Android-4.0.2-dev.163-debug.apk`

APK SHA-256: `f4b269ef3861dd1f371b532384afb23931eef349000479e4996582867989fd41`

APK audit: `com.peerchat.android`, version code 2, version name `4.0.2-dev.163`, ARM64, minimum SDK 26, target SDK 35. Signature verifies with the accepted certificate SHA-256 `db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`. `FCM_CONFIGURED=true`; accepted notification configuration remains local and uncommitted. The Windows ZIP was checked for required runtime files and absence of profile databases, private keystores/certificates and Firebase source configuration.

## Physical acceptance checklist

1. Extract Windows into a new folder and use the existing profile. Update Android with `adb install -r` and the APK above. Do not uninstall, clear data or change trust/grants.
2. With an already authorized group, close Android, send several group messages from Windows, reopen Android and confirm recovery with original authors/order and no duplicates.
3. Reverse the direction: close Windows, send from Android, reopen Windows and confirm import. Restart both and repeat reconciliation.
4. With a third authorized member available, leave the original source closed and check missed-message recovery from the remaining member.
5. Check reference-only attachment entries show unavailable private metadata/time/order without claiming a download. Existing authorized Windows attachment bubbles should remain single and usable; expired cache content must not be represented as recoverable merely because a reference exists.
6. Confirm normal DM/group messages, attachment saving, clipboard screenshots, emojis and Android background notifications still work. Send diagnostics if recovery stalls; do not reset profiles or permissions.

## Limits and stopping point

Historical epochs without authenticated retained membership evidence or required text keys remain unresolved. Existing unresolved DM provenance is not reassigned or deleted. Production same-account group enrollment gates remain unchanged; this does not automatically enroll or trust another device. Group-control reconciliation is not a fabricated global progress percentage in the same-account archive screen.

References alone cannot provide private filenames, original time/order, decryption material or evicted ciphertext. Android reference display does not add a new group-attachment download engine. Previously documented accepted-baseline host-fixture limitations are not represented as newly passing physical tests.

Stop for Windows/Motorola physical acceptance. No Android Voice, unrelated roadmap work, merge, push, deployment or live-data mutation is included.
