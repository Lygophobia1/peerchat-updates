# Compatibility screen-share default — dev.79

The only production behavior change is the desktop screen preference initializer:
new profiles and valid older preferences lacking a Backend field now select
WebRTC Compatibility. Explicit Native Windows and WebRTC Compatibility selections
remain unchanged. Saved FPS, bitrate and cursor settings remain unchanged.
Native Windows remains available; its implementation and known crash were not
investigated or changed. Voice, TURN, networking and session ownership are unchanged.

## Automated evidence

- Preference tests: missing file, missing Backend with existing quality settings,
  and 16 reloads of each explicitly saved backend passed.
- Exact dev.79 ScreenMedia/AudioProcessing/AudioReceive adapters: Compatibility
  capture, H.264, RTP, decode and presentation startup passed after host voice
  connected. Both voice endpoints continued receiving Opus packets and remained
  connected. This uses injected PCM and local test ICE, not physical microphone,
  quality or TURN acceptance. Native screen startup was not exercised.
- Existing media-performance and archive Settings regressions passed, preserving
  dev.78 diagnostics and the accepted Settings fixes.
- Full Windows build and package/import audit passed.
- Android JVM tests, debug/test/release builds and lint passed for matching version
  metadata; no Android behavior change or device install was performed.
- Development version/updater audit passed, including dev.78 -> dev.79 ordering,
  safe extraction, manifest and exact runtime hashes. Unsigned Development policy
  only; not published.
- git diff --check passed.

## Candidate

Windows client: build/development/4.0.2-dev.79/Client/PeerChat.Desktop.exe

Updater package: build/development/4.0.2-dev.79/DevelopmentUpdater/PeerChat-4.0.2-dev.79.zip

SHA-256: a7bd119dc6947169d0f569eb699fa36a23137864ee65d1ba3935c048af62925a

## Physical validation pending

Use a new profile or one without a saved backend preference: Settings should show
WebRTC Compatibility. Start a real voice call, then screen share and verify video
and audio continue. Stop and restart sharing. Confirm an existing explicit Native
selection remains selected; do not trigger the known Native crash as part of this
default-only test. Switching the setting must affect the next share, not an active
media generation, through the unchanged existing selection behavior.
