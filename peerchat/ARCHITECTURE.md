# PeerChat architecture

> Documentation authority
>
> - Product goals and roadmap: `PEERCHAT_DEVELOPMENT_GOAL.md`
> - Current implementation status: `PROJECT_STATUS.md`
> - Current system architecture: `ARCHITECTURE.md`
> - Discovery protocol contract: `docs/DISCOVERY_PROTOCOL.md`

## Components and ownership

Stage 8 has a separate `windows_media/PeerChat.ScreenMedia.dll` built with MSVC and a narrow C
ABI. It enumerates monitors/windows and hardware codec capabilities, captures the selected source
through WGC/D3D11, emits complete H.264 Annex-B access units from a bounded hardware-encoder
queue, and decodes/presents received units through a bounded Media Foundation/D3D11 path. The
standalone `ScreenShareMedia` media-generation component connects those units to a pinned
libdatachannel H.264 RTP track over ICE/DTLS-SRTP. Neither component owns PeerSession, trust,
share lifetime, signaling authorization or recovery policy. Profile-scoped `ScreenShareManager`
owns ShareId/lifetime/generation, one active share, stale-event rejection and bounded recovery;
media teardown marks the generation stopped before detaching libdatachannel callbacks, synchronizes
late remote-track registration, and drains an in-flight RTP callback before closing its decoder.
`Node` sends share-control messages only through the verified winning peer-TLS binding, independently
of whether that reliable binding is direct or server-relayed. Media generation uses separate ICE;
the server does not see screen content. WPF uses the C ABI for source selection, viewer presentation,
profile-scoped quality preferences and a compact status strip. Detailed once-per-second observational
telemetry is kept in Security/Diagnostics, with unsupported ICE statistics marked unavailable.
The isolated Development package includes the MSVC adapter; the proven MinGW core build and
dependencies are unchanged. Physical two-PC acceptance is pending.

`desktop/App.cs` owns the WPF presentation. `desktop/CoreAdapter.cs` calls the C-compatible
exports in `ui_bridge.cpp`; the bridge owns the C++ `peerchat::Node`. The native core owns
P2P sockets, Schannel sessions, peer trust, reconnect, delivery acknowledgements, logical ordering,
offline outbox, contacts, identity keys, encrypted local history, and the client-owned group ledger.

App.cs observes Windows network-address and discovery/friend changes and supplies current LAN/public
candidate observations plus authenticated relay availability and the current network generation.
The native profile-scoped `SessionTransportManager` is the single decision owner for direct/relay
attempt tokens, ordered route attempts, bounded relay fallback, active winner, duplicate arbitration,
retry/backoff, promotion/drain, route cancellation and stale-callback protection. Node retains its one
worker, resolvers, sockets and Schannel objects and executes manager decisions. `DirectTransportAdapter`
wraps each legacy connection and reports lifecycle/capability observations without owning a socket.
The profile-scoped `DeliveryManager` owns durable DM/group send eligibility, transport-scoped ACK
authorization, resend eligibility and incoming-DM deduplication above individual connections.
`SessionTransportManager` composes `DirectHandoff` for capability-gated direct-route coordination;
the latter owns transaction state but no socket, scheduler or authoritative channel binding.
Media continuity remains outside the implemented boundary.

`desktop/RelayTransport.cs` is the concrete server-relay byte-stream adapter. It has no route policy:
it opens the certificate-pinned outer server TLS connection only when the manager emits a relay
attempt, applies the returned framed session binding, and passes opaque bytes between that stream and
the native core. Relay reads are long-lived and may remain idle indefinitely; adapter shutdown closes
the socket to interrupt the read, while writes keep the bounded socket timeout used for backpressure.
The native core runs its existing Schannel peer TLS session inside those bytes, so peer identity/trust
and application framing stay above both direct and relay physical transports.

The local-only types distinguish the profile scope, local/verified-remote device identity,
runtime session epoch, transport instance ID, independent route revision, channel requirements,
transport states, send acceptance, and immutable value events. Reliable control/messages and
realtime media are distinct channel kinds. The manager promotes a direct connection only after the
existing mutual-TLS, expected-identity and local-trust checks succeed. The same active direct binding
serves control and reliable messages; only then may its instance submit or acknowledge durable
delivery. Media has no logical binding yet. Stale session epochs, transport
instances, route revisions and network generations cannot change the active binding. Send acceptance
explicitly does not mean delivery; receive events contain sizes/metadata, never borrowed payload buffers.

The C ABI assumes serial ownership and returns borrowed wchar_t buffers from its bridge instance.
Node owns one worker and bounded command/event queues; the current event queue drops its oldest
entry at capacity. Relevant lifecycle changes emit self-contained session snapshots containing the
profile/session epoch, monotonically increasing revision, network generation, active connection and
state. WPF applies only newer revisions, so a late loser/attempt event cannot overwrite a live winner.
WPF polls native events and separately renders media telemetry. Winsock, Schannel, CNG, DPAPI,
Windows DNS/text APIs and WinMM remain concrete adapters/dependencies, not a portable Android core.

`server/PeerChat.Server.cs` is the headless account/discovery service. The separate
`PeerChat.Server.Admin.exe` creates/reuses a development certificate, launches the server,
forwards bounded logs, and uses a local named pipe for status and graceful stop. The server does
not require the Admin application.

The WPF conversation shell keeps normal actions in the selected conversation header. DMs expose a
compact Call action; an active call expands that header into a slim state/duration/mute/deafen/end
strip. Engineering call details remain in that call area and load only when the user opens Details
and expands Advanced / Diagnostics; the expanded content is height-capped and scrolls internally.
The right side panel is a stable, compact global Security & Status surface. Conversation details
open from the DM header, while group members, invitations, settings, rename/leave, and applicable
member administration open in anchored group-header flyouts. Only one header flyout is active at a
time, and group creation remains available from the Groups tab regardless of selection. These are
presentation projections over the existing view models and do not introduce a second call, group,
connection, or polling path.

## Identity and trust boundaries

- **Account identity:** username, login, friend relationships, discovery, and presence.
- **Local profile:** stable-ID local account workspace selected before native-core initialization.
- **Device/Peer ID:** persistent public cryptographic identity derived from the device TLS key.
- **Local trust:** an explicit account/device association stored only in the selected profile.
- **Endpoint candidate:** a temporary network route. An IP address is never an identity.
- **Local history:** encrypted client-side messages and delivery/order metadata.
- **Server:** optional social/discovery coordination; it is not required for local profile access.

Of device key material, the server receives only public identity. Private device keys, plaintext
messages, chat keys and local history never go to discovery. Passwords are sent only inside
register/login TLS requests, produce salted PBKDF2-SHA256 server verifiers and are never persisted
as plaintext or used as chat-encryption keys. Session tokens are random,
stored hashed on the server, and DPAPI-protected per local profile on the client. The protected
`account-link.dat` stores the durable account ID/username binding and optional current session; a
sign-out clears the session while preserving the binding. An unreadable binding fails closed in
Offline / Direct P2P mode instead of being treated as a new unlinked profile.

A Fresh Client reset creates a new device identity. Presence registers it for the signed-in
account. Friends receive that current identity as the expected identity for a direct connection.
If TLS presents a different identity, the client shows one explicit verification prompt keyed by
expected identity, presented identity, and connection generation. Acceptance persists the new
local trust; rejection suppresses repeats for that attempt only.

## Profile bootstrap and lifecycle

Normal startup reads `%LOCALAPPDATA%\PeerChat\profiles.json`, shows profile cards, and resolves the
selected stable ID to `Profiles\<profile-id>`. Only after selection does the desktop construct/start
the C++ adapter. The native core opens that directory's identity, trust, encrypted SQLite history,
contacts/outbox, endpoints, and settings. Account-session restore and discovery authentication occur
after native Ready and must resolve to the account ID already bound to that profile. A valid protected
session automatically signs in; an unlinked new profile offers Sign In, Create Account, or Continue
Offline. An unavailable server leaves the profile in Offline / Direct P2P mode with local history,
manual connections, and saved direct reconnects available.

The chooser uses flat dark selectable rows showing profile name, linked `@handle` when present, and
last-used time. Creation is an explicit `+ New Profile` editor state; Open Profile is the primary
action, while Rename and confirmed recoverable Delete live in the selected row's overflow menu.
Keyboard selection, double-click/open, profile locking, and the bootstrap order are unchanged.

Profile switching remains restart-based. The active client stops discovery events/heartbeats, P2P
sessions and listener, joins/releases the native worker off the UI thread, flushes/closes SQLite,
and only then starts the chooser in a replacement process using the current executable path and
installation root. The temporary chooser uses an explicit WPF lifetime so closing it cannot end
the application before the selected profile's main window opens. Each process opens only one selected profile. In-memory friend state,
unread counts, endpoint caches, connection generations, and pending trust prompts therefore cannot
cross the switch. Before native startup, the process acquires an exclusive lock file inside that
profile directory. A second process targeting the same profile exits with a clear message instead
of contending for DPAPI/SQLite state; processes targeting different profiles may run concurrently.
If two distinct profiles contain the same saved listener port, the first keeps it and a later
profile atomically binds an available port and persists that port before emitting native Ready.
An explicit command-line port override remains strict and fails instead of silently changing.
Profile removal is confirmed and recoverable under `RemovedProfiles`.

Account ID and device Peer ID remain separate fields so several devices per account can be added
later. The server refuses to attach a device Peer ID already owned by one account to another account.
The current profile owns one durable account binding, one optional protected session, and one device
identity; multi-device synchronization and automatic trust transfer are not implemented.

## Client-owned group state

The native `GroupLedger` is the authority for each local group replica. A random 128-bit Group ID is
created on the owner client and never derived from a name or issued by the discovery service. Create,
rename, invite, accept/decline, leave, remove, role, and message operations are content-addressed,
ECDSA-signed events. Authorization is evaluated against the deterministically reconstructed prior
state, ordered by `(logicalOrder, eventId)`. The same authenticated event set therefore converges to
the same metadata, membership, roles, and message order even when peers receive concurrent or causal
events in different orders. Duplicate event IDs are replay-safe; conflicting or invalid signatures
do not alter visible state.

Membership and invitations are maps keyed by account ID, with a verified device Peer ID and public
key attached to each current member record. Owner, admin, and member permissions are checked for each
event. Collections, fanout, storage, and the WPF member list do not encode a three-member limit. Any
future practical size/rate policy must remain configurable resource policy rather than a protocol or
schema assumption.

Accepted events are stored in the selected profile's DPAPI-encrypted SQLite database. A per-peer
group outbox survives disconnect/restart and sends signed group envelopes only over authenticated
mutual-TLS peer connections that advertise the signed-groups capability. Inviting a verified friend
queues the accepted ledger history to that device; later accepted events fan out across current
members/invitees, so connected members can forward events to temporarily indirect peers. Pending
causal events are reconsidered when predecessors arrive. Each process owns one profile/core/ledger
set, and group keys, events, outboxes, and account binding are cleared with the rest of native state
on a profile restart. Separate processes may own separate profiles.

The WPF layer invokes groups through the existing C ABI and renders replicated state, including
pending invite count in the group list and named `Pending Invite` rows in group details; it does not
become an authority. Discovery may help locate a friend's account/device endpoint for an invitation,
but the server creates no Group ID, membership row, role, metadata, invite, removal, or history. If
discovery disappears, persisted groups and direct group messaging continue. The Stage 5.5 reliable
relay can carry the same inner peer-TLS stream in Automatic mode; Direct Only remains available and
Relay / Privacy mode is still future work. Transport choice cannot change the logical group or its
client authority.

Conversation history is retrieved for presentation in bounded newest-first pages (100 messages),
not by materializing an encrypted DM history or a group's complete signed ledger into WPF. The
desktop anchors a selected conversation at its newest loaded message and requests an older page only
when the reader reaches the top. Existing message-ID deduplication and deterministic logical ordering
remain the presentation merge boundary. Group pagination reads a page from the already-validated
local ledger; it does not alter ledger validation, storage, or replication.

## Endpoint model

The two network roles are independent:

| Concept | Meaning | Port source |
|---|---|---|
| Discovery bind address | Local interface used by the account service, commonly `0.0.0.0` | Server `Port` |
| Advertised discovery endpoint | Hostname/address clients use for account/discovery TLS | `PublicHostname` + server `Port` |
| Client P2P listener | Native direct-chat listener, commonly `0.0.0.0:7777` | Client profile |
| LAN P2P candidate | Current private/local address for that listener | Client presence + client listener port |
| Public P2P candidate | Public address mapped to that listener | Observed/configured address + client listener port |
| Self-hosted P2P fallback | Optional public address for a client colocated with the server | `SelfHostedPublicP2PHost` + client listener port |

The discovery port never becomes a P2P candidate port. A client may use `localhost` for discovery
while advertising a non-loopback LAN P2P candidate. The server rejects loopback, unspecified,
private, link-local, shared, multicast, and reserved observed addresses as public candidates.
When discovery arrives over loopback, the server uses the explicit self-hosted P2P fallback only
if the advertised LAN address belongs to a server-host interface. Remote authenticated clients
may use a validated server-observed public IPv4 plus their advertised P2P listener port.

Presence stores the LAN/public candidates, source, observed-address classification, current device
identity, and last-seen time. Only authenticated friends receive endpoint candidates. Public
username search returns public profile/device fields without endpoints.

## Candidate selection and direct connection

One WPF friend-snapshot reconciliation path serves normal refresh, friend dialogs and manual repair.
Concurrent refresh callers share its task and responses are rejected after account sign-out/change.
Try Reconnect refreshes presence/friends when possible, then invokes the existing candidate selector.
Explicit retry retires stale attempts for that expected identity; unchanged presence does not destroy
an in-progress trust handshake. Wrong-identity suspension begins at detection, before socket cleanup.
Group invite candidates come from `ChatViewModel.GroupInviteCandidates`, keyed by established account
membership rather than endpoint/presence. The open picker updates on friend/group model events.

Opening a friend DM consumes the authoritative friend snapshot. The client compares a private LAN
candidate against a freshly enumerated snapshot of active local IPv4 interfaces using each
interface's real subnet mask. Enumeration and mask failures are isolated per adapter/address so a
broken virtual adapter cannot make a later valid Ethernet/Wi-Fi subnet appear remote. Automatic
presence refresh also re-detects the advertised physical LAN address after network changes; an
explicit user-configured endpoint remains unchanged. Per-peer diagnostics retain every evaluated
local address/prefix, the candidate, the match result, and why an interface was ignored.
WPF publishes a same-subnet LAN candidate and a valid public candidate without selecting between
them. The manager tries LAN before public, advances promptly after route failure, and may request an
authenticated relay after an eight-second bounded direct window. TLS identity mismatch suspends that
attempt behind the existing trust boundary and never triggers silent rebinding.

The WPF conversation identity is `account:<account-id>`, not an endpoint, session, presence state,
or device Peer ID. A DPAPI-protected index inside the selected profile attaches current and known
device Peer IDs to that one social conversation, allowing encrypted native histories for those
devices to merge in the same view. An authoritative friend snapshot updates the existing name and
current candidates. A changed current Peer ID remains unverified until the existing native trust
flow explicitly accepts it; old trust is never transferred silently.

The chosen endpoint is passed through the existing adapter to the C++ P2P transport with the
friend's expected Peer ID. TCP, TLS, peer authentication, and explicit trust remain distinct
states. Once authenticated, direct chat no longer depends on discovery. Advanced → Direct P2P
remains a permanent server-independent fallback.

For reliable control/text fallback, both clients authenticate their account/device to the server and
request reciprocal attachment to the exact expected account/device pair. The server permits this only
for established friends or a current pending friend request, returns a random short-lived 43-character
session ID and opposite peer-TLS roles, then forwards frames whose prefix matches that session. Frames
are capped at 64 KiB; the native client relay queue is capped at 1 MiB and server writes are synchronous,
so slow consumers exert socket/TLS backpressure instead of creating an unbounded application queue.
The server terminates only outer account TLS. It cannot authenticate the remote peer on the client's
behalf and cannot decrypt the inner peer TLS. Duplicate, stale, third-party and mismatched-device
attachments are rejected. The inner verified Peer ID remains authoritative; changing direct/relay
route for the same key does not prompt again, while a changed key uses the existing verification UX.

While relay is active, direct probes continue on manager-owned bounded backoff. Failed candidates do
not affect the relay winner. A fully authenticated direct candidate uses the existing Stage 5 handoff
before promotion and relay drain. Relay never replaces a healthy direct winner. Stage 6 call-control
frames can use either authenticated reliable winner; WebRTC audio remains an independent direct
ICE/DTLS-SRTP path and is never carried by the reliable server relay.

Each native outbound attempt has an ID plus expected Peer ID, concrete endpoint, candidate source,
and route. Saved-contact failures use exponential retry backoff with per-attempt jitter from five
seconds up to a five-minute cap. A changed presence/candidate or Try Reconnect can request an
immediate attempt without clearing trust or failure history. If an endpoint presents a different
Peer ID, that attempt remains behind explicit verification; rejection suspends automatic retry for
that endpoint until a manual retry or candidate update. A fresh authoritative candidate matching an
already-running endpoint updates that attempt's source/route metadata in place rather than leaving
it labelled as a saved-contact attempt or interrupting an active trust decision.

## Discovery events and presence

Login/reconnect fetches one authoritative friend snapshot, then opens a long-lived authenticated
TLS event stream. Events are invalidation notifications; SQLite friend/account state remains
authoritative. Event reconnect uses bounded backoff and triggers a repair snapshot. A slow safety
snapshot runs every three minutes while the channel is healthy or one minute while reconnecting.

A separate 60-second presence heartbeat updates the current device/candidates only. Repeating an
identical heartbeat does not broadcast another friend event. Presence expires after 90 seconds and
the server emits an offline notification.

Windows `NetworkAddressChanged` is the event-driven runtime recovery trigger; bursts are debounced
without polling. A real active-address/subnet fingerprint change increments the managed network
generation, requests native invalidation for that exact generation, cancels obsolete discovery work,
and removes saved private endpoints outside every current subnet while retaining contact identity,
trust, conversation, history, and outbox state. The client then publishes its newly detected candidate,
forces a fresh authoritative friend snapshot, and runs the same candidate selector again.
Discovery-channel recovery performs publish-before-snapshot as well, so an interface change that
occurred while discovery was unreachable is repaired on reconnection. The manager compares the
request's network generation with each route observation: delayed invalidation from an older network
cannot close a newly authenticated route. Exact-endpoint refresh also retains a healthy winner.

An out-of-subnet saved private endpoint is not retried while discovery is unavailable. Identity and
conversation state survive. Rapid network changes are coalesced through WPF recovery flags. Route
removal can still call clearVoice when the manager confirms that the active control winner belongs
to the invalidated generation; there is no media-independent control-gap policy yet. Automated tests
prove stale-generation rejection, but the false-route-teardown backlog stays open pending a physical
live LAN→hotspot→LAN transition.

Authenticated duplicate arbitration prefers a newly proven session over an older session with no
recent transport progress, then uses the stable Peer-ID direction rule for genuinely simultaneous
fresh connections. The stale heuristic is over two seconds without recorded transport progress;
an idle connection is not thereby proven failed. Ordinary startup duplicates and legacy peers retain
this immediate-arbitration path. A deliberate alternate endpoint for a verified handoff-capable peer
can instead create one candidate while the current binding remains active. Retry state stays dormant
while a usable winner exists. Superseded duplicates emit diagnostic lifecycle events without marking the
surviving conversation offline. Final disconnect events carry an explicit bounded origin such as
remote EOF/TLS close, local route invalidation, socket error, authentication timeout, protocol
rejection, or duplicate arbitration. Discovery presence remains advisory and never closes an
otherwise live authenticated P2P session merely because a snapshot reports offline.

The managed connection projection applies the native manager's complete session snapshots in
monotonic revision order. A snapshot pins active connection ID, connected state, session epoch and
network generation. Attempt, retry, trust, lifecycle and error events remain secondary diagnostics;
once a snapshot has arrived they cannot replace the winner or downgrade `ConversationModel.Online`.
Only a newer authoritative snapshot can clear that state. The event queue remains bounded, but each
snapshot is self-contained rather than a delta that depends on every prior lifecycle event.

## Current peer protocol and delivery ownership

Peer frames retain version byte 1 and the existing Hello, Accepted, text, ACK, group and voice
formats. Optional Hello capability bit 16 enables direct handoff. Bit 32 is a per-connection
replacement-intent marker: only an outbound attempt deliberately created alongside a known
handoff-capable winner advertises it. This distinguishes simultaneous startup duplicates from
intentional overlap. Peers without bit 16 receive no new packet types and retain legacy arbitration.

Storage persists the encrypted DM/group outboxes and message IDs/order independently of sockets.
The profile-scoped `DeliveryManager` loads those pending entries and records which authenticated
transport instance submitted each ID to each exact destination. A DM or group ACK may change durable
state only when that same destination and an authorized submitting transport present the ID; another
peer, an unsent ID, or an unrelated transport cannot acknowledge it. A transport marked draining
cannot accept new DM/group submissions, but can still complete ACKs for IDs it previously submitted.
Retiring a transport removes only its runtime ACK eligibility, so still-pending data can be resent on
an authenticated replacement and survives process restart through the existing encrypted storage.
Incoming DM storage and the group ledger retain their existing message/event-ID duplicate suppression.

### Stage 5 direct handoff contract

The optional `Handoff` frame (type 14) contains a phase number and two length-prefixed ASCII strings,
using the existing Writer/Reader encoding. The phase is 0–5; each string is at most 64 characters
(maximum body 156 bytes). Frames are processed only after mutual TLS, expected-device verification,
local trust and remote Accepted. Current contact/key trust is checked again on receipt; promotion
and fallback also require a live, non-cancelled, trusted manager record. Contact removal revokes
handoff eligibility. No discovery operation or application message format changed.

| Phase | Authenticated exchange and effect |
|---|---|
| 0 — Hello | Each connection exchanges a fresh 16-byte UUID-shaped nonce from the existing BCrypt-backed ID generator (122 random bits) as 32 hex characters; the second string is empty. |
| 1 — Prepare | The lower verified Peer ID coordinates the first ready candidate. The first string is the candidate nonce pair, ordered by Peer ID; the second is the retained old connection's nonce pair. |
| 2 — Ready | The responder matches both connection lifetimes and the retained binding, then echoes the transaction and old binding. |
| 3 — Commit | The coordinator confirms readiness. The responder promotes locally, puts the old route into draining, and replies. |
| 4 — Promoted | The coordinator receives promotion acknowledgement, promotes locally, and sends Confirm. |
| 5 — Confirm | Confirms completion to the responder and begins its bounded old-route drain. |

The nonce pairs identify transactions and connection lifetimes; they are not credentials. Local
profile/session epochs and instance IDs remain local and are independently checked at the owner.
Replies from another instance, another transaction or an old epoch cannot promote a route. Replays
of a current exchange receive idempotent replies without extending deadlines. The coordinator
serializes simultaneous intentional candidates; competitors are retired without demoting the old
winner. Replacement capabilities must cover the active bundle's capabilities.

Preparation/confirmation is limited to 10 seconds, phase retries to one per 500 ms, and completed
draining to 3 seconds. Unselected authenticated candidates expire after 10 seconds. The existing
worker services these deadlines; no new polling thread or reconnect scheduler exists. Existing
connection/queue limits and the delivery owner's 16-submission backpressure still apply.

Only the active binding receives new DM/group outbox submissions. Previously submitted frames and
authorized ACKs can finish on the draining route, with storage/ledger deduplication handling overlap.
If the new route fails while the old route is still retained, live, trusted and not cancelled, the
manager restores the old binding and the delivery owner resumes its submissions. Otherwise normal
reconnect/backoff and encrypted pending delivery apply. Exhausted coordination closes the candidate
and preserves/restores a usable old binding; it does not promise uninterrupted recovery after all
routes have failed. Socket EOF/error remains stronger evidence than advisory discovery state.

For handoff-capable established bindings, network/candidate invalidation preserves a usable winner
while fresh endpoint selection may prepare an alternate. Explicit cancellation still invalidates
obsolete candidate instances; old-generation callbacks cannot promote them. Ordinary duplicates
remain immediate retirement. Stage 7 observes this authoritative control availability but does not
own, duplicate, or alter the Stage 5.5 transport recovery policy.

## One-to-one voice boundary

Text, group envelopes and call signaling use the authenticated reliable peer-TLS binding, whether
its physical transport is direct TCP or the Stage 5.5 opaque server relay. Voice
media uses an independent libdatachannel WebRTC/ICE/DTLS-SRTP path, which may select a different
endpoint pair. Reliable transport kind is not a proxy for media capability. Temporary final-control
loss is projected independently and receives a bounded grace period: healthy media may continue,
but new negotiation waits for a restored authenticated direct or relay control binding. Local hangup
always stops local media immediately. If unavailable control and media exceed their bounded recovery
deadline, `CallManager` terminates the logical call cleanly.

Voice call signaling is carried only after the existing peer connection has completed mutual
TLS, expected Peer ID verification, and explicit local trust. Protocol capability negotiation gates
voice support. Random 128-bit call IDs identify ephemeral calls; they are not social identities and
are never persisted. The profile-scoped native `CallManager` owns the authoritative typed call ID,
monotonic runtime lifetime, direction, participants, timeout, mute/deafen state and one-call-per-profile
state machine: Idle, Outgoing, Incoming, Negotiating, ConnectingMedia, Connected, Ending, Ended or
Failed. It handles answer, decline, cancel, hang-up, busy, timeout, deterministic simultaneous-call
convergence, peer disconnect, and profile shutdown. Duplicate/completed IDs are bounded for replay
suppression. The participant collection is a future extension point only; Stage 6 implements no group
membership, multi-peer negotiation, mesh, SFU or group UI.
Audio-device errors no longer mark the authenticated text peer offline. Media-device startup is
owned by the node's `VoiceMedia.poll` lifecycle, not a libdatachannel callback. Teardown detaches
callbacks, stops audio, explicitly deletes independently registered C API track handles, then deletes
the peer connection and codecs. Reuse resets call media counters and mute/deafen state. Each call
creates a distinct PeerConnection/ICE agent tagged with the current WPF network generation and
active IPv4 fingerprint; candidates are cleared with call teardown. A meaningful network-generation
change or media disconnect now interrupts only the current media generation. A capture generation
check prevents a buffer from being requeued after mute closes/reopens its device.

The authenticated channel carries bounded SDP offer/answer and trickled ICE candidates tagged with
the call ID. Capability-gated recovery frames additionally carry the monotonically increasing media
generation. Each asynchronous media event is bound to call ID, native call lifetime, expected
participant and media generation, so a stale candidate, answer, connected or failed completion cannot
mutate a replacement generation. Each send resolves the current
authenticated reliable winner, while call state remains keyed by call ID and stable Peer ID, so
duplicate arbitration cannot strand negotiation on a retired socket. Diagnostics correlate the
winning connection at call start and the connection used
for each offer, answer, and candidate without recording SDP. `VoiceMedia` is a one-call native
engine which reports media facts/events and never owns call state: libdatachannel 0.24.3 owns direct ICE,
ECDSA DTLS-SRTP, RTP and jitter handling; libopus 1.5.2 encodes 16 kHz mono 20 ms capture frames
with the required 48 kHz RTP clock advanced by 960 ticks per frame; the event-driven WinMM engine
owns selected capture/playback devices. Capture resolves the Windows default through
`waveInMessage(DRVM_MAPPER_PREFERRED_GET)` and validates its concrete ID against current WinMM
enumeration before `waveInOpen`; the mapper sentinel is used only for the query. Default fallback
uses concrete enumerated devices and honors Windows preferred-only policy. Explicit capture choices
are never replaced by another mic. Refresh preserves a matching ID/name, then matches by name when
enumeration has reordered. Capture is opened before playback. Physical evidence and any remaining
device-specific acceptance belong in PROJECT_STATUS.md, not this implementation contract. Only Opus/RTP crosses the
media transport. Raw PCM stays in memory and is never logged, persisted, or written to temporary files.

libdatachannel represents the negotiated audio m-line as one reused bidirectional `sendrecv` track.
PeerChat installs receive/RTCP handling and open/closed callbacks directly on that track; it does not
assume the library will report a second remote track. Capture may begin with the peer connection, but
Opus submission is gated on the negotiated track being open.
The pinned 0.24.3 C API media interceptor is intentionally unused: its implementation moves input
messages to a local result vector without returning that vector, breaking delivery. Diagnostics use
track callbacks and explicitly label pre-track transport counters unavailable rather than intercepting
media. The pinned dependency itself is unchanged.

Callbacks from libdatachannel and capture are reduced to bounded native queues/events consumed by
the existing node worker. The stable C ABI exposes call lifecycle, refreshable device enumeration/
selection, mute, deafen, and an on-demand bounded diagnostic snapshot. That snapshot contains only
safe call state, device labels, media counters, timestamps, and the latest error. Capture diagnostics
include requested/resolved ID/name, bounded enumeration/attempts, exact waveInOpen MMRESULT, default
query result, and fallback use, retained in the most-recent-call snapshot. On-demand capture details
also report prepare/queue/start results, worker event counts, completed buffers/bytes, header state,
Windows capture position, and actual playback device. ICE diagnostics include gathering, connection,
and signaling state, bounded host/server-reflexive candidate classes, candidate exchange counts,
network generation, and selected pair. These are metadata only; no audio samples are read for
diagnostics and no new polling is introduced. It never contains
audio or cryptographic/session secrets. WPF orchestrates controls and duration. Profile switching disposes the old core,
closes peer connection/audio devices, then releases identity/database state. Discovery is not in the
established media path. Standard STUN gathering supplements offline-capable host candidates for
common NAT traversal. Recovery recreates a fresh pinned-library PeerConnection rather than assuming
unsupported in-place ICE restart behavior. `CallManager` owns Interrupted/Recovering state, a
30-second production deadline and retry pacing; `VoiceMedia` still reports facts only. The same
logical CallId survives successful replacement, and mute/deafen, selected devices and software gain
are reapplied. Older peers without the media-recovery capability retain the Stage 6 terminate-on-loss
behavior and never receive the new frame types. STUN is coordination only; TURN remains configured
as a future `VoiceMedia` route class but no TURN server is configured or deployed. The PeerChat
server relay carries recovery signaling only and never carries audio.

The bridge exports typed authoritative route facts. Reliable control reports Direct LAN, Direct
Internet or PeerChat Server Relay from the active `SessionTransportManager` route. Voice reports
Direct LAN ICE, server-reflexive/NAT-traversed ICE or TURN only from the selected media candidate
pair; unavailable/recovering state and media generation come from `CallManager`. WPF renders those
facts separately with Peer TLS versus DTLS-SRTP and does not infer route type from display strings.

Live level meters use a generic `MediaTelemetry` snapshot rather than exposing PCM to managed code.
The existing native capture worker calculates normalized RMS before its Opus callback; decoded PCM
updates output level as it enters the existing playback queue. Native time-based decay returns stale
levels to zero. The node publishes two atomic floats plus active/input/output availability flags, and
the existing 60 ms WPF event tick renders read-only meters and human-readable states. Muting,
deafening, call teardown and profile shutdown force the appropriate level to zero. No second device
path, audio retention, sample logging, diagnostic-string parsing or additional timer is involved.
WPF maps normalized RMS onto a -60 dBFS to 0 dBFS display range so quiet decoded speech remains
visible without changing gain or playback volume.

Profile voice preferences are stored separately as a small CurrentUser-DPAPI-protected record. They
contain only input/output device ID and name plus bounded 0–200% software-gain values; call IDs,
state, participants and audio are never persisted. Missing devices resolve to the current Windows
default on load. Gain is applied with saturating signed-PCM scaling in the existing native capture
and mixed-playback paths before encode and after decode respectively. Settings exposes no AEC,
noise suppression, AGC, input sensitivity, push-to-talk or microphone-test switch because the
current WinMM/Opus stack does not implement those facilities.

## Storage, migration, reset, and backup

Each stable profile under LocalAppData contains identity, DPAPI-protected configuration/session
state, protected account/device conversation mappings, bounded safe connection diagnostics, trusted
contacts, encrypted SQLite history/WAL/SHM, outbox, message metadata, and unread state. Installation
metadata contains profile IDs/names/last-used state, but no keys or messages.

DPAPI failures retain their underlying Win32 result in the local error text for diagnosis, without
logging plaintext, key material, entropy, or protected bytes. Current-user DPAPI scope and the
existing record/entropy format remain unchanged so existing encrypted profile data stays compatible.

Connection recovery remains event-driven through the existing candidate selection and native
session path. Native attempt/schedule/failure events carry their own expected identity and route
metadata; WPF stores presentation state per expected peer rather than in a global current-attempt
slot. The UI records only account/device identifiers, candidates/source/route, TCP/TLS/trust results,
retry outcomes, and timestamps. It never records credentials, keys, TLS secrets, or message text,
and trims the per-profile log rather than allowing unbounded growth.

First profile-aware startup copies legacy root state to a timestamped `LegacyBackups` recovery
location and a staging profile. File lists, lengths, and SHA-256 hashes are verified before a
completion marker and stable directory are activated. Catalog replacement is atomic; a completion
marker repairs interrupted catalog activation. Source state is not deleted, ciphertext is copied
without decrypt/re-encrypt, logs contain no secret values, and no server is contacted.

Backup copies exactly the selected protected profile to `PeerChatProfile`. Reset backs up and
replaces only that stable directory, so only it gets a new Peer ID. Restore stages/verifies the
backup, moves current state to a safety backup, and swaps only the selected directory. Other
profiles are untouched. Full-install reset requires explicit selection.

Server runtime data remains under the configured ProgramData directory; certificate private keys
remain in `CurrentUser\My`. Client reset never modifies server accounts, friendships, databases,
or certificates.

## Firewall

After the native Ready event reveals the actual P2P listener port, the client ensures one inbound
TCP rule named `PeerChat P2P (Private)`, scoped to the packaged executable and Private networks.
The rule belongs to the installation, not a profile. A hidden, non-elevated query first validates
its enabled state, inbound direction, Private-only scope, TCP protocol, Allow action, exact current
executable path, and listener port. An exact match is reused without running an elevated modify
command; elevation occurs only when the rule is missing or stale. Successful verification is
recorded in install-wide `firewall-state.dat`, but the live Windows rule remains authoritative.
It never enables Public-network access or disables Windows Firewall. Firewall/TCP diagnostics
remain separate from TLS and identity failures.
In-process UI test hosts have no profile manager/install boundary and therefore never provision or
rewrite the machine-wide rule for their harness executable path.

## Repository and release layout

`desktop`, `server`, and `shared` contain managed source; root C++ files remain where active
build scripts expect them. `tests`, `docs`, and `scripts` contain their named support files.
Build output is separate from retained release artifacts; scripts still default to the v0.3-beta
layout. PROJECT_STATUS.md identifies the accepted v4 candidate and retained fallback packages.
`shared/VersionInfo.cs` centralizes the actual runtime version, currently `4.0.0-dev.2`.
The packaged Client always carries the desktop EXE/config, native core, updater EXE/core/config,
and update-feed map as one runtime set. Packaging rejects a missing required managed file and inspects
the desktop assembly reference metadata before running the native audit. The packaged `PeerChat.Core.dll` statically contains the pinned native media/security dependencies
and MinGW support runtimes; it may dynamically import only Windows inbox DLLs. The managed bridge
preloads the exact adjacent core with DLL search restricted to its directory and System32, reports
the Windows loader code/message on failure, and does not rely on `PATH`. Packaging fails unless a
managed runtime-set check, import-table audit, and restricted-search native load smoke all pass.

## Client updates

The updater is a separate managed executable packaged beside the client, but the desktop copies its
three updater runtime files to `%LOCALAPPDATA%\PeerChat\Updates` before launching it. It therefore
never tries to rename a directory containing its own loaded executable. The desktop-side
`DesktopUpdateClient` is a narrow service boundary: it owns installation-wide channel preference,
feed selection, check/download/prepare actions, and updater launch; profiles never own update
configuration or payload state.

An update manifest binds product, channel, semantic version, upgrade floor, HTTPS package URL,
size, SHA-256, entry executable, release-notes URL, and publication time. Release manifests sign a
canonical UTF-8, LF-delimited representation of those fields (including schema version) with
RSA-3072/SHA-256 PKCS#1 v1.5. The signature itself is excluded from the signed bytes. Because the
signed SHA-256 and byte size bind the ZIP, the signature authenticates the exact package bytes. The updater
rejects unsafe manifests/ZIP paths, downgrades, oversized downloads/extractions, bad hashes, and
unhealthy first launches. It journals staged replacement, retains a sibling backup, launches the
new client, and waits for the desktop to write its health token only after native local-profile/core
startup succeeds. Failed health confirmation restores and restarts the prior installation.

Before moving the old Client directory to its sibling backup, the installer waits for the original
PeerChat process to exit. That single rename retries only transient Windows sharing/lock failures
on a short bounded backoff; every attempt, delay, success, or final failure is logged. No retry is
applied to validation, extraction, replacement activation, health confirmation, or rollback.

Release is the default channel and fails closed for an absent signature, unknown/revoked key ID,
wrong algorithm, malformed signature, invalid signature, or package/hash/size mismatch. The updater
embeds only a key-ID-indexed public-key registry; the Release private key remains in the release
operator's Current User Windows certificate store. Development may use the existing explicit unsigned
opt-in only for authorized Development packages; HTTPS, size, and SHA-256 verification remain mandatory.
Key rotation overlaps keys: a release signed by the old key first distributes the new public key,
the publisher changes keys only after adoption, and a later signed release retires the old key.
Feed hosting and update delivery stay outside discovery, profiles, peer sessions, and transport.
