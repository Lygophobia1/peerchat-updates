# PeerChat Test-Gate Status

This file classifies test entry points for the normal development build gate. It does
not delete historical tests or declare a failing assertion unimportant.

## Current

- `tests/test-group-ciphertext-fanout.ps1` — Stage 1C real isolated TLS server,
  three authenticated non-friend event channels, byte-identical opaque routing,
  bounded requests, authorization, epoch/removal, duplicate/receipt semantics,
  online-only disconnect/reconnect and sender recipient-list rejection.
- `tests/test-group-text-ui.ps1 -ClientDirectory <current Client>` — compiled
  group projection/sender/time/unread/history, logical group isolation and
  honest server-admission versus E2E receipt wording.

- `tests/test-group-message-envelope.ps1 -IncludeStage1A` — Stage 1B AES-GCM
  envelope/signature, bounded tamper parsing, current roster/device/key checks,
  durable replay and nonce reservation, protected history and Stage 1A scoped
  peer-TLS key-handoff/removal plus three-device authenticated text/decrypt/history
  regressions. Actual server fanout is tested separately above; no group media.
- `tests/test-audio-processing.ps1` — pinned standalone APM presets/lifecycle,
  restricted-PATH processing and import audit.
- `tests/test-voice-presets.ps1` — profile-local processing controls and preset
  persistence, effective native configuration, media-generation isolation, raw
  bypass and unchanged 20 ms Opus transport.
- `tests/test-voice-settings-ui.ps1` — actual WPF On/Off switches, discrete
  strength choices and disable/preserve/re-enable behavior, with isolated profiles.
- `tests/test-h264-rtp.ps1` — deterministic screen RTP ordering/loss/FU-A and
  reference recovery, plus pinned libdatachannel packetizer/RTCP PLI regression.
- `tests/test-updater.ps1` — updater/package behavior.
- `tests/test-desktop-ui.ps1` — WPF smoke test, including unique per-run result files
  and child exit/timeout reporting.
- Focused feature regressions run by their owning checkpoint, such as TURN, screen
  media, and diagnostics-retention checks.

## Legacy/stale — excluded from the normal diagnostics gate

- `tests/storage_test.cpp` — currently reports `Altered ciphertext was accepted`.
  This needs a separate storage-integrity investigation under the interactive Windows
  user context. It is not a diagnostics acceptance result.
- `desktop/AdapterTest.cs` — its `Share format` expectation predates the current
  screen-share adapter contract. It needs an explicit historical-test review before
  its assertion is updated or it returns to a normal gate.

## Needs review

- `scripts/run-tests.ps1` remains the broad historical regression entry point. It
  still invokes the two items above and must not be used as the normal diagnostics
  gate until their individual reviews are complete.
- Any test added by a feature checkpoint should be classified here before being added
  to `build.ps1 -Tests`.
