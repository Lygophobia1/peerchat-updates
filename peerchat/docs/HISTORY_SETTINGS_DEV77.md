# History Settings startup regression — 4.0.2-dev.77

## Proven cause

`HistorySettingsPanel` called `render()` before its asynchronous authenticated
refresh. Its local `account` was zero while application-level `historyDevices`
still contained the previous authenticated snapshot. `HistoryName(0, device)`
passed that unset account into `HistoryDeviceNames.Get` and then `Key`, whose
strict account/device validator threw `Invalid device metadata scope`.

There is no new string scope involved: the cosmetic metadata contract is a
positive account ID plus an exact 64-character lowercase hexadecimal Peer ID.
The same cached-device regression reproduces the exception on the preserved
dev.76 executable and passes on dev.77.

## Fix and safety

- Initial rendering waits for the panel's account/generation to bind to the
  authenticated snapshot; cached rows are not used under an unset account.
- Optional nickname reads with missing/invalid context return the caller's
  display fallback, without reading another account's names. Writes retain
  the original strict scope and nickname validation.
- Unreadable protected nickname records produce a fallback and inline warning,
  not a startup modal. Successful retry clears the warning.
- A rename refuses to overwrite an unreadable existing nickname file.
- No schema migration, identity change, recipient-key change, authorization
  change, grant revocation, cursor reset or history deletion is performed.
- Unfinished real-time media instrumentation was preserved separately and is
  NOT part of this physical-test package/checkpoint.

## Automated validation

- Settings WPF smoke: 32 cached-device panel initializations, missing names,
  unset account, invalid/null device identity, account-isolated names, 32
  nickname reopen cycles, rename isolation, strict invalid writes, protected
  malformed nickname, corrupt protected file, no corrupt-file overwrite,
  retry/reopen, history and advanced-section controls.
- Archive enrollment, authorization, provenance, session scope, protected
  history/restart/replay/cursors, revocation/re-enrollment, and signed server
  metadata tests pass. Server metadata requires a separate authorization-mode
  invocation, not the provenance fixture mode.
- Packaged archive application boundary passes, including five native profile
  worker restarts, unchanged registration identity, explicit consent and
  account rejection; paired host workers pass scoped TLS/catalog download.
- Full Windows build passes; existing compiler warnings remain.
- Android JVM tests, debug/test/release builds and lint pass. Android behavior
  is unchanged; only unified version metadata advances.
- Version/package/import/updater extraction and runtime hashes pass; Android
  ARM64/JNI/dependency audit passes. Development unsigned policy only; nothing
  published. Diff whitespace check passes.

## Artifacts / physical gate

Windows: `build/development/4.0.2-dev.77/Client/PeerChat.Desktop.exe`

Updater: `build/development/4.0.2-dev.77/DevelopmentUpdater/PeerChat-4.0.2-dev.77.zip`

ZIP SHA-256: `7B3BC9A4D1D6116330498F43AB5DFC0C7002CF4BA8F29803AAE883C0BC8152B6`

Android: `build/development/4.0.2-dev.77/Android/PeerChat-4.0.2-dev.77-debug.apk`

APK SHA-256: `BDCF5646A3FF16D41D973A2F95F964F6307F31072545E2A09F628ECF2A6866C8`

Replacement install succeeded on the authorized moto g power (2025);
installed version verified as 4.0.2-dev.77. No uninstall, data clear, identity
rotation or protected-record deletion was performed.

Physical retest pending: on an existing Windows profile, sign in, open Settings
repeatedly, navigate Privacy & Security / History Sync and Advanced, inspect
existing devices/permissions and rename/reopen a nickname. Confirm no startup
modal and unchanged history, identity and permissions. No new grant or revoke
is needed merely to test this regression.
