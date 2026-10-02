# Group screen sharing: physical-test candidate

The accepted one-to-one mechanics were extracted in checkpoint `0f50b85`
(development build `4.0.1-dev.58`). Native Windows and WebRTC Compatibility
one-to-one selection remain unchanged. Group rooms use Compatibility only:
WGC capture, one software Media Foundation H.264 encoder, existing
libdatachannel RTP/DTLS-SRTP and TURN, and the accepted H.264 recovery/MFT/D3D11
receiver. This is not full libwebrtc calling or an SFU.

## Ownership

- The existing server group-room service owns membership, sharer/viewer roles,
  room epoch, revisions and per-participation media-generation tokens.
- `GroupScreenShareService` coordinates local application commands and room
  snapshots using the existing application pulse and authenticated discovery
  event/signaling channel. WPF reads immutable published room statuses;
  native pump/stop work does not execute in a UI snapshot getter.
- Each local room participation has one `GroupScreenShareSession`. A sharer
  owns one reusable capture/encode source. A viewer owns no encoder. Separate
  rooms have separate session lifetimes; voice participation is independent.
- `IScreenShareMediaTopology` consumes borrowed encoded access units and
  authorized endpoints. `RelayMeshScreenShareTopology` owns the individual
  viewer media routes, not room authority or reliable sessions. The session
  accepts a fake/alternative topology without concrete RTC dependencies.
- Each route reuses `ScreenShareMedia` for independent SSRC/packetizer,
  bounded send pressure and receiver recovery/decode/presentation state.
  External-source sender routes cannot create capture/encoders. An AU is
  drained once and synchronously submitted to each eligible route.

No second reliable transport, SessionTransportManager, socket channel,
reconnect scheduler, group decoder fork or encoder-per-viewer is introduced.
Native ScreenMedia decoder/encoder internals and the D3D11 multithread fix are
unchanged. Receiver initialization uses the same 1280x720 bootstrap as the
accepted one-to-one adapter; existing MFT stream-change negotiation obtains
the actual received SPS dimensions. Bootstrap dimensions are not a capture cap.

## Authorization and lifetime

The existing server media-signaling handler now permits screen-room signals
only between the current authenticated sharer and an authorized current
viewer. It checks account/device, server session binding, group roster,
room epoch/revision and both participation tokens. Viewer-to-viewer screen
signaling is denied. Voice-room signals retain their existing authorization.
Client signal consumption is filtered by group and room, so voice and screen
owners do not consume each other's frames.

The current sharer's participation token is the share generation. Local
bindings additionally include room epoch and local participation. A join or
leave reconciles just the affected route; it does not restart the source or
existing routes. Route IDs and media generation reject replaced routes.
Removal, authoritative participation ending, event-channel loss or room close
retire media rather than silently resurrecting old participation. Stop/rejoin
is the initial explicit recovery action for a failed viewer route.

Production topology requires validated relay-only ICE configuration. It
rejects direct policy, direct trickle candidates and embedded non-relay SDP
candidates. Credentials remain transient configuration, never diagnostic or
room state. Fresh configuration is obtained for new routes when required;
credential issuance expiry does not tear down established routes.

Capture/encoder failure terminates that local share; viewer transport failure
remains route-local. Meaningful failures freeze native diagnostics before
teardown. The existing transition-only crash marker also covers group screen
participation and is completed when participation ends, including room close.

## Diagnostics and bounded limits

Group Screen Share diagnostics combine existing authoritative room state with
local session snapshots: room/share generation, role, backend/topology, state,
capture/encoder pipeline counts, captured/encoded frames, queue drops,
keyframes, dimensions, fanout copies, active routes and individual route
state/reason/TURN classification/bytes/decoded/presented counts. Existing
receiver detail includes RTP ordering/loss/AU prevention, IDR recovery and
decode/presentation counters. No key bytes, PCM or screen contents are logged.

Capture/encode FPS and encoded-drain Mbps are session-lifetime averages.
Per-route send API bytes are **not** wire upload measurements. RTP callback
bytes are receive-side measurements. Mesh upload grows with viewer count even
though capture/encode work is shared. This candidate limits topology to 32
authorized destinations; pending candidate/signaling queues and per-pulse AU
drain work are bounded. It is not a large-room scalability claim.

Automated fake-source tests verify pipeline count remains one with 1/2/3
viewers. A real local two-viewer test proves shared software encoding, RTP,
MFT output/presentation and survival of another viewer's removal. That local
fixture uses explicitly test-only direct ICE, not production TURN acceptance.
Physical CPU/memory/upload and receive/decode/presentation rates for 1/2/3
TURN viewers remain to be measured; no synthetic visual acceptance is claimed.

## Focused tests

- `tests/test-group-screen-share.ps1 -DevelopmentDirectory <build>`:
  deterministic session/fake-topology fanout, incremental join/leave,
  isolation, generations, capture failure, independent rooms; production
  privacy/authorization/queue guards and 15 topology lifetimes; actual
  shared-source two-receiver software H.264/RTP/MFT presentation.
- `tests/test-group-media-rooms.ps1`: server role/device/session/token
  authorization, room lifecycle and three real authenticated TLS event
  clients including screen signal forwarding and voice/screen isolation.
- `tests/test-screen-shared-backend.ps1 -DevelopmentDirectory <build>`:
  reusable mechanics plus accepted Native, Compatibility and mixed
  one-to-one media regressions.
- Existing H.264 RTP reorder/loss/FU-A/IDR tests, group text/envelope/fanout,
  group voice/session/topology, voice presets and packaged Group UI smoke.
- Full authoritative build stages matching current-source Client/Core,
  ScreenMedia, Server/Admin/Updater and validated unchanged audio artifacts;
  import and restricted-PATH native load audits are part of packaging.

## Physical validation

Use matching new Client packages and the new Server build; an older server
will not authorize screen-room signaling. Keep accepted older builds intact.

1. A starts a screen room. B selects Watch without joining voice.
2. C selects Watch. Verify both receive continuous video and A diagnostics
   report one capture pipeline/encoder, two active TURN routes and increasing
   fanout copies. Verify receiver RTP/AU/decoded/presented counters advance.
3. B stops watching; C must continue. B rejoins; C must remain uninterrupted.
4. Move windows/high-motion content; verify recovery without persistent
   corruption. Test a failed viewer without interrupting other routes/chat.
5. A stops, then starts again. Old-generation media must not reappear and
   viewer windows must close/reopen cleanly through voluntary Watch.
6. Remove a member and close a room; unauthorized routes must end. Verify
   voice continues independently. Optionally share two rooms simultaneously.
7. Record CPU/memory, source FPS/bitrate, upload and per-viewer receive/decode/
   presentation metrics at 1/2/3 viewers. Physical validation is pending.

Native group sharing, SFU, automatic backend fallback, AEC and DM redesign
remain out of scope.
