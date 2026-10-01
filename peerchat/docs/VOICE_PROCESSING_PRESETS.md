# Voice processing presets

The Voice settings UI uses stable friendly names and does not expose WebRTC
parameters. The native Audio Processing component owns the centralized mapping:

| Setting | Preset | WebRTC APM mapping |
| --- | --- | --- |
| Noise Suppression | Low | `NoiseSuppression::kLow` |
| Noise Suppression | Balanced | `NoiseSuppression::kModerate` |
| Noise Suppression | High | `NoiseSuppression::kHigh` |
| Automatic Gain Control | Gentle | Adaptive digital AGC, 6 dB compression limit |
| Automatic Gain Control | Balanced | Adaptive digital AGC, 9 dB compression limit |
| Automatic Gain Control | Strong | Adaptive digital AGC, 12 dB compression limit |

The gain target and limiter remain at the pinned WebRTC revision's defaults.
Tune only the native mapping after physical listening tests; keep UI labels and
profile storage semantics stable. Echo cancellation remains disabled until the
real render-reference and stream-delay path is implemented.

On/Off switches enable each feature. Strength remains a discrete three-choice
selector; disabling a feature grays its selector without changing the saved
preset. Changes apply to the next call/media generation. The stored NS preset
identifier 1 (`PC_APM_NS_MEDIUM`) is displayed as Balanced.

The preset-enabled C ABI extends `pc_apm_config` and `pc_apm_stats`. Core and
Desktop must be staged with this matching DLL, not an older APM artifact. The
existing size checks reject mismatched structures and retain raw-PCM bypass.

Validated artifact: `C:\Peerchatdep\artifacts\audio-processing\webrtc-6cf6bcb-voice-presets\x64\PeerChat.AudioProcessing.dll`

SHA-256: `C324896121259FBCE10C6B8B15995606378CF281EEDD7515F0B4680181A75A08`

WebRTC remains pinned to `6cf6bcb585b31414aac02377df2bc8fec770d0e8`.
The isolated clang-cl APM target reuses the existing dependency build. Normal
`build.ps1 -All` stages this hash-verified artifact without rebuilding WebRTC.
The original standalone artifact and accepted screen-share packages are retained.

Focused verification: `tests/test-audio-processing.ps1` (restricted-PATH APM
lifecycle and imports), `tests/test-voice-presets.ps1` (profile persistence,
effective APM presets, generation/cadence/Opus transport), and
`tests/test-voice-settings-ui.ps1 -ApplicationDirectory <new Package folder>`
(actual WPF switches, selectors, OFF/re-enable and unavailable AEC).
