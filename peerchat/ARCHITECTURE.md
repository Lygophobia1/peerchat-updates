# PeerChat architecture

> Documentation authority
>
> - Product goals and roadmap: `PEERCHAT_DEVELOPMENT_GOAL.md`
> - Current implementation status: `PROJECT_STATUS.md`
> - Current system architecture: `ARCHITECTURE.md`
> - Discovery protocol contract: `docs/DISCOVERY_PROTOCOL.md`

## Components and ownership

`desktop/App.cs` owns the WPF presentation. `desktop/CoreAdapter.cs` calls the C-compatible
exports in `ui_bridge.cpp`; the bridge owns the C++ `peerchat::Node`. The native core owns
P2P sockets, Schannel sessions, peer trust, reconnect, delivery acknowledgements, logical ordering,
offline outbox, contacts, identity keys, encrypted local history, and the client-owned group ledger.

`server/PeerChat.Server.cs` is the headless account/discovery service. The separate
`PeerChat.Server.Admin.exe` creates/reuses a development certificate, launches the server,
forwards bounded logs, and uses a local named pipe for status and graceful stop. The server does
not require the Admin application.

## Identity and trust boundaries

- **Account identity:** username, login, friend relationships, discovery, and presence.
- **Local profile:** stable-ID local account workspace selected before native-core initialization.
- **Device/Peer ID:** persistent public cryptographic identity derived from the device TLS key.
- **Local trust:** an explicit account/device association stored only in the selected profile.
- **Endpoint candidate:** a temporary network route. An IP address is never an identity.
- **Local history:** encrypted client-side messages and delivery/order metadata.
- **Server:** optional social/discovery coordination; it is not required for local profile access.

The server receives public device identity only. Private device keys, plaintext messages, chat
keys, password plaintext, and local history never leave the client. Passwords produce salted
PBKDF2-SHA256 server verifiers and are never chat-encryption keys. Session tokens are random,
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

v0.3-beta switching is restart-based. The active client stops discovery events/heartbeats, P2P
sessions and listener, joins/releases the native worker off the UI thread, flushes/closes SQLite,
and only then starts the chooser in a replacement process using the current executable path and
installation root. The temporary chooser uses an explicit WPF lifetime so closing it cannot end
the application before the selected profile's main window opens. Only one selected profile is opened. In-memory friend state,
unread counts, endpoint caches, connection generations, and pending trust prompts therefore cannot
cross the switch. Profile removal is confirmed and recoverable under `RemovedProfiles`.

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
causal events are reconsidered when predecessors arrive. Only one profile/core/ledger set is open,
and group keys, events, outboxes, and account binding are cleared with the rest of native state on a
profile restart.

The WPF layer invokes groups through the existing C ABI and renders replicated state; it does not
become an authority. Discovery may help locate a friend's account/device endpoint for an invitation,
but the server creates no Group ID, membership row, role, metadata, invite, removal, or history. If
discovery disappears, persisted groups and direct group messaging continue. A future relay may carry
the same encrypted signed envelopes in Direct Only, Automatic, or Relay / Privacy transport modes,
but transport choice cannot change the logical group or its client authority.

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

Opening a friend DM consumes the authoritative friend snapshot. The client compares a private LAN
candidate against every active local IPv4 interface using that interface's real subnet mask.
Same-subnet candidates are preferred; otherwise a valid public candidate is selected. A LAN route
failure may fall through to public promptly. TLS or identity failures do not trigger route fallback.

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

## Discovery events and presence

Login/reconnect fetches one authoritative friend snapshot, then opens a long-lived authenticated
TLS event stream. Events are invalidation notifications; SQLite friend/account state remains
authoritative. Event reconnect uses bounded backoff and triggers a repair snapshot. A slow safety
snapshot runs every three minutes while the channel is healthy or one minute while reconnecting.

A separate 60-second presence heartbeat updates the current device/candidates only. Repeating an
identical heartbeat does not broadcast another friend event. Presence expires after 90 seconds and
the server emits an offline notification.

## One-to-one voice boundary

Voice call signaling is carried only after the existing direct peer connection has completed mutual
TLS, expected Peer ID verification, and explicit local trust. Protocol capability negotiation gates
voice support. Random 128-bit call IDs identify ephemeral calls; they are not social identities and
are never persisted. The native core owns the one-call-per-profile state machine: Idle, Outgoing or
Incoming Ringing, Connecting, Connected, Ending, then Idle. It handles answer, decline, hang-up,
busy, timeout, deterministic simultaneous-call convergence, peer disconnect, and profile shutdown.
Duplicate or completed call IDs are bounded in memory for replay suppression.

The authenticated channel carries bounded SDP offer/answer and trickled ICE candidates tagged with
the call ID. `VoiceMedia` is a one-call native boundary: libdatachannel 0.24.3 owns direct ICE,
ECDSA DTLS-SRTP, RTP and jitter handling; libopus 1.5.2 encodes 16 kHz mono 20 ms capture frames;
the event-driven WinMM engine owns selected capture/playback devices. Only Opus/RTP crosses the
media transport. Raw PCM stays in memory and is never logged, persisted, or written to temporary files.

Callbacks from libdatachannel and capture are reduced to bounded native queues/events consumed by
the existing node worker. The stable C ABI exposes call lifecycle, device enumeration/selection,
mute, and deafen; WPF orchestrates controls and duration. Profile switching disposes the old core,
closes peer connection/audio devices, then releases identity/database state. Discovery is not in the
established media path. The current implementation uses host candidates only; STUN/TURN/privacy
relay remain future transport additions and cannot become call authority.

## Storage, migration, reset, and backup

Each stable profile under LocalAppData contains identity, DPAPI-protected configuration/session
state, protected account/device conversation mappings, bounded safe connection diagnostics, trusted
contacts, encrypted SQLite history/WAL/SHM, outbox, message metadata, and unread state. Installation
metadata contains profile IDs/names/last-used state, but no keys or messages.

Connection recovery remains event-driven through the existing candidate selection and native
session path. The UI records only account/device identifiers, candidates/source/route, TCP/TLS/trust
results, retry outcomes, and timestamps. It never records credentials, keys, TLS secrets, or message
text, and trims the per-profile log rather than allowing unbounded growth.

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
`build\v0.3-beta` is replaceable output, `releases\v0.3-beta` is the active package, and
`releases\v0.1-beta` is the immutable fallback. `shared/VersionInfo.cs` centralizes the version.
The packaged `PeerChat.Core.dll` statically contains the pinned native media/security dependencies
and MinGW support runtimes; it may dynamically import only Windows inbox DLLs. The managed bridge
preloads the exact adjacent core with DLL search restricted to its directory and System32, reports
the Windows loader code/message on failure, and does not rely on `PATH`. Packaging fails unless an
import-table audit and restricted-search native load smoke both pass.
