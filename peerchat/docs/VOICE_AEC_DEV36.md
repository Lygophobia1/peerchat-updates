# Windows voice AEC milestone — dev.36

Ownership: existing VoiceMedia / GroupVoiceSession -> existing Audio device
worker -> capture APM -> Opus. Device-paced final remote playout, after group
mixing, deafen and software output gain, is the reverse/render reference.
The microphone is never mixed into that reference. No speaker loopback,
additional audio thread, reference queue, call owner or reconnect owner exists.

The existing pinned WebRTC 6cf6bcb585b31414aac02377df2bc8fec770d0e8 adapter already
exports ProcessReverseStream, stream-delay and reset operations. Enable its
desktop echo_canceller (AEC3), then existing NS/adaptive-digital AGC. The adapter
DLL and accepted hash are unchanged; no new dependency or license is introduced.
WebRTC remains under its existing BSD-style notices/dependency audit.
16 kHz mono, two 160-sample capture/reverse blocks, 320-sample/20 ms Opus and
48 kHz RTP clock remain unchanged. TURN-only production media policy is unchanged.

The reference is supplied only after successful WinMM output submission, with a
bounded delay estimate from queued output buffers plus completed capture backlog.
WinMM does not expose a precise hardware/acoustic presentation timestamp.
This is an estimate, not a calibrated sound-card delay; real speaker A/B testing
is still required. No sample-rate conversion is needed in the accepted 16 kHz path.
Reference inactivity/underruns are measured; no unbounded buffering is added.

Mute/unmute and deafen transitions request a DSP reset on the device worker.
Device/media-generation replacement creates a new APM instance. Shutdown joins
the audio worker before destroying DSP state. Remote join/leave changes only the
existing receive mix. Processing failure remains audio-scoped raw PCM bypass and
uses the existing unified failure reporting, not transport recovery.

Profile-local AEC toggle is available; old profiles retain OFF (the previous
behavior). Master processing OFF bypasses it. Changes apply on the next call or
media generation. NS and genuine 6/9/12 dB AGC presets use discrete horizontal
selectors, with saved strengths preserved/disabled when OFF.
The legacy processing C ABI remains; the new versioned entry carries AEC.
Group bridge uses an additive pre-start option, not mid-call reconfiguration.

Bounded diagnostics include configured AEC, reference active, capture/render
blocks, reference underruns, resets, estimated delay and latest APM error.
No raw audio is persisted or logged.

Validation:

- Eight DSP generations with AEC ON/OFF, 150 capture/render frames each, exact
  two-block cadence, reset, malformed length and raw failure bypass.
- Existing preset/profile tests plus AEC persistence/legacy OFF defaults.
- Bidirectional 1:1 ICE/DTLS-SRTP/Opus/NetEq runs with AEC ON/OFF, unchanged
  packet framing, mute/deafen/volumes and 100 receive lifetimes.
- Group one-member/multiple-source mixing/fanout, AEC render/capture cadence,
  reset and 30 media lifetimes.
- Actual Audio worker test: reference contains final output-gain-adjusted PCM,
  bounded delay, no mid-generation replacement, no callback after teardown.
- Relay-only topology authorization/direct-candidate rejection and 15 lifetimes.
- Actual WPF settings smoke: switches, segmented choices, OFF disable/gray,
  saved strengths, AEC persistence, master toggle.
- Full build/package/import audit, Android 94 host/UI tests, debug/test/release,
  lint, ARM64/JNI/dependency audits, version/updater/hash/extraction and diff checks.

Physical 2-person/group speaker echo and TURN voice quality remain pending.
Previous dev.34 real TURN DTLS-SRTP infrastructure success is not represented as
dev.36 physical voice acceptance. No screen/media algorithms were changed.

Pi handoff: existing validated dev.34, PID 15754, TCP 7443 listening;
/opt/peerchat-test/server/4.0.2-dev.34/;
/opt/peerchat-test/config/4.0.2-dev.34/server-config.json;
/opt/peerchat-test/data/4.0.2-dev.33/physical-client.db.
Certificate SHA-256:
62A181282FE940F03F6A35541220A05F9ED2CB85A41F6DE3B0DF111286BA17E3.
Coturn PID 2718 remains untouched; TURN/FCM external configuration/secrets preserved.
No server behavior change/deployment is required by client-side AEC.
No merge or publication.
