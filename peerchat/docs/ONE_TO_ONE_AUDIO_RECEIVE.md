# One-to-one WebRTC receive candidate

The live one-to-one receiver uses the same VoiceReceiveEngine and pinned
PeerChat.AudioReceive.dll as accepted group voice. Authenticated DTLS-SRTP RTP
goes directly to NetEq, WebRTC Opus and AudioMixer, not immediate opus_decode
or the legacy Audio PCM queue. No receiver selector, extra jitter buffer,
timer or reconnect owner is introduced.

CallManager owns call ID, remote device, lifetime and media generation. Node
only forwards these into VoiceMedia's immutable ReceiveBinding. Exactly one
source is authorized. SSRC is bound within that source, not used as identity.
Track handles locate retained callback contexts; teardown invalidates/removes
them before releasing devices/transport. In-flight callbacks retain only the
old context/engine and cannot feed a replacement call.

The Windows output completion worker requests 320 samples: two 160-sample
10 ms pulls at 16 kHz mono. RTP remains 48 kHz/960 ticks per outgoing packet.
Deafen advances NetEq and discards output; mute stays capture-side. Output
gain remains after playout. Capture, NS/AGC/input gain, Opus sender, 20 ms
packets, TURN policy and signaling are unchanged. AEC is OFF/unavailable.

BUSY drops the affected packet or supplies silence without blocking retries.
Fatal receive errors latch silenced playback and the existing audio error
event, never transport recovery. The output meter measures provider PCM.
Diagnostics report binding, one-source count, accepted/rejected/BUSY packets,
paired pulls, delay, concealment, discard/flush and exposed FEC counters.
Decoded frames and separate late/reordered totals are explicitly unavailable:
NetEq pulls include PLC and are not proof of successful packet decoding.

## Automated validation

test-voice-presets.ps1 covers bidirectional RTC receive, unchanged capture
presets/framing and 100 one-source lifetimes with concurrent RTP/stop, retained
stale callbacks, sequence/timestamp wrap, deafen and buffer guards.
test-audio-receive.ps1 covers pinned adapter reorder/loss/duplicate/late/wrap,
PLC/FEC configuration, fixed buffers and repeated/concurrent teardown. FEC
recovery counters are reported honestly; enabled FEC is not proof of use.
Existing group voice/topology, rooms, group text and screen regressions remain
separate gates. No group-screen source changes belong to this candidate.

## Physical acceptance still required

Compare with immutable dev.61: simultaneous quiet/loud speech, mute/deafen,
volume and all NS/AGC presets, repeated hangup/re-call, 10–30 minute TURN call,
jitter, latency/crackle, responsive hangup and clean shutdown. Automated
success is not physical quality acceptance. dev.61 / cdcded9 remains the
pending group-screen physical-test candidate.
