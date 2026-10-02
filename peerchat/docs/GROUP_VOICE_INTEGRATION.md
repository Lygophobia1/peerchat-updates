# Group voice integration — physical-test candidate

Live group voice is wired, but **three-client physical acceptance is pending**.
The accepted one-to-one VoiceMedia and screen implementations remain unchanged.
The dev.48 authoritative room model and dev.50 receive proof are the foundations.

## Implemented integration boundaries

`GroupMediaRooms` publishes a public random `mediaGeneration` for each joined
account/device. It is distinct from the private event-session binding. Unrelated
joins leave existing tokens intact; leave/rejoin or session replacement changes
the token. Disconnect, move, removal and close remove its authority.

`group_media` travels on the existing authenticated event stream. Its bounded
fields are request/group/room IDs, server epoch, current group room revision,
roster generation, source/target participation generations, target account/device,
opaque route ID, positive transport media generation, kind (offer/answer/candidate/
close), data and MID. The server supplies source account/device from the session,
not client fields. Only two currently joined, roster-authorized devices in the
same voice room can exchange these frames. No friendship, DM permission, new
socket owner or reconnect loop is introduced. The existing event write deadline
bounds forwarding. Admission and delivery are serialized with room mutations and
roster authorization. Per-participation signaling is limited to 256 frames/10 s.
Successful forwarding means signaling delivery to the event stream, not audio.

`DiscoveryClient` maintains bounded event-channel queues (64 outbound/in-flight,
128 inbound/results). In-flight signaling expires after 15 s on the existing
worker; disconnect drops it, without rejoining. Incoming frames may wait up to
10 s for a racing authoritative snapshot. They are revalidated at consumption;
stale epoch, roster, participation or device cannot authorize media. The media
owner must additionally bind route/media generations and validate SDP/ICE.
SDP/ICE are transport metadata, never diagnostic report content.

`LocalVoiceSource` provides one capture-side APM/Opus encoder, consuming existing
volume-adjusted 640-byte/320-sample PCM. Two 160-sample APM blocks feed one 20 ms
Opus packet, with a 960-tick RTP timestamp advance. One returned encoded packet
can be replicated by a topology; no destination owns APM/encoding. Presets and
raw-PCM bypass reuse the existing APM boundary. AEC remains off. The lifecycle
owner must stop capture before closing it; only the capture worker processes.

`VoiceReceiveEngine` is reusable, not group-specific. It maps opaque authenticated
source keys and generations to independent NetEq handles, parsing bounded RTP
headers/extensions/padding without prior Opus decoding. Equal SSRCs on different
authorized sources do not merge identities. A changed SSRC within a source
generation is rejected. It performs two 10 ms mixed pulls into one preallocated
320-sample buffer. There is no custom adaptive PCM queue or receive timer.
Deafen discards output after pulling, so old audio is not replayed on undeafen.
Try-lock failure produces silence and increments underrun blocks, not a busy loop.
The group session instantiates this component; accepted one-to-one receive is
not migrated in this checkpoint.

AudioReceive config version 1 retains the original deterministic simulated clock.
Config version 2, with the same C ABI struct layout/exports, uses WebRTC's real
clock. Packet arrival is measured inside the adapter; paired device callback pulls
do not depend on caller-provided wall clock or exact 10 ms thread wakeups. The
output device must still be the sole production pacing owner. The changed adapter
is built against the existing pinned WebRTC cache; APM/algorithms are unchanged.

## Live ownership boundary

```text
authoritative voice-room participation
                |
         GroupVoiceSession
          /            \
 LocalVoiceSource   VoiceReceiveEngine -> one device-paced output
        |
 IVoiceMediaTopology
        |
 RelayMeshVoiceTopology -> authorized TURN media endpoints
```

`GroupVoiceSession` receives explicit server-authorized room participation and
owns one capture/encoder, one receive engine and one device. The topology accepts
encoded audio and attributed RTP only. Incremental membership preserves existing
endpoint identities. Candidate policy is always relay-only, including validation
of embedded SDP candidates. Unexpected remote tracks cannot become other sources.
An endpoint failure is reported independently, without rebuilding the room.

`GroupVoiceService` owns application command orchestration and projects immutable
high-level room status. It uses the existing application pulse/event channel,
not a new socket, timer or reconnect scheduler. The native C ABI is opaque handles,
primitive inputs and copied strings only. Native operations run off the dispatcher.
Room/source authorization is rechecked against current authoritative snapshots.
Signaling retries only a stale-revision rejection, at most four attempts within
ten seconds; participation replacement invalidates pending signaling. Startup
failure is latched until an explicit leave/rejoin, rather than repeatedly rebuilding
audio/networking. Safe failure details are frozen before native teardown and handed
to existing unified failure retention. APM failure retains raw capture bypass.

The final Group Details pane replaces Media Rooms (state only). It shows members,
multiple voice/screen rooms, room names, participant names/counts, Join/Leave,
mute/deafen and group management. Header creation commands use the authoritative
server group ID, not the legacy ledger ID. WPF does not consume topology, RTP,
NetEq or TURN objects. The pane retracts and restores without changing DM state.
Group screen video is explicitly unavailable; room state is not simulated video.
Diagnostics remain outside this pane. Find People restores lease-derived online/
offline availability without disclosing device IDs or granting friendship/trust.

The current build stages the verified AudioReceive adapter in Client/Package and
audits its imports and restricted-search loading. Its SHA-256 is
`82621EF0C83C6F4844DF0871FE05D7E637F0287179B94CC19CDF272C9DB40C01`.
WebRTC remains pinned to `6cf6bcb585b31414aac02377df2bc8fec770d0e8`.

Future SFU replaces topology only: one encoded upstream stream, independently
attributed remote sources into the same receive engine. Room authority, source
identity, APM, mixer/device ownership and mute/deafen remain unchanged. After group
media acceptance, one-to-one receive can intentionally migrate to this receiver;
the current split is a temporary physical comparison boundary, not a second
permanent audio architecture.

## Validation so far

- `tests/test-group-media-rooms.ps1`: three real TLS event sessions, same-room
  signaling delivery, cross-room/nonmember/nonparticipant/wrong-device denial,
  stale epoch/revision/lifetime rejection and stable incremental membership.
- `tests/test-audio-receive.ps1`: original deterministic loss/reorder/FEC/PLC and
  lifecycle proof plus real-clock paired pulls with irregular timing.
- `tests/test-voice-receive-engine.ps1`: actual MinGW-to-DLL C ABI under restricted
  PATH; encode once/two source inserts; 120 APM calls for 60 packets; same-SSRC
  isolation, retire-one/preserve-other, deafen, bounds and 50 receive lifetimes.

These are boundary/component tests, not end-to-end group voice, actual device
underrun measurements, TURN mesh performance or physical success. Required real
2/3/4-participant route and CPU/bandwidth measurements remain pending.

Additional regressions cover the actual session with fake device/topology (one
member, incremental sources, one encode/fanout, mute/deafen, retired callbacks,
30 lifetimes), real RTC endpoint guards/lifetimes with an unavailable local TURN
fixture (15 lifetimes), and synthetic WPF group presentation/collapse. Existing
one-to-one voice, encrypted group roster/envelope/fanout and H.264 tests remain
the comparison gates. No simulated test is physical acceptance.

## Physical validation

Use the matching Server and Package from the candidate revision on three clients.
Create two voice rooms in a server-authorized group. Join room A alone: it must
remain active. Join a second, then a third member voluntarily and verify all
directions of speech using headphones. Group Voice diagnostics must show increasing
capture/receive/mixed counters and TURN Relay per established source, never a
direct route. Leave/rejoin one member while the other two keep speaking; move
between A/B without cross-room audio; exercise mute/deafen, removal, sign-out,
stop/restart and continued reliable/group messaging. Check screen rooms remain
independent and Watch is unavailable. Record CPU, bandwidth and playout busy/silence
blocks for 2/3/4 participants; component benchmarks are not live performance.

Known limits: mesh capacity is 32 remote sources per local receive engine (not a
roster/room protocol limit); failed routes require explicit leave/rejoin in this
first candidate. No SFU, AEC, group screen video or one-to-one receive migration.
Starting group audio while an existing direct call is active is unavailable;
the accepted direct-call commands themselves are unchanged.
