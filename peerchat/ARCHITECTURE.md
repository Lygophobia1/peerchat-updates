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
offline outbox, contacts, identity keys, and encrypted local history.

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
stored hashed on the server, and DPAPI-protected on the client.

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
after native Ready. An unavailable server leaves the profile in Offline / Direct P2P mode with
manual connections and saved direct reconnects available.

v0.3-beta switching is restart-based. The active client stops discovery events/heartbeats, P2P
sessions and listener, joins/releases the native worker, flushes/closes SQLite, and then starts the
chooser in a replacement process. Only one selected profile is opened. In-memory friend state,
unread counts, endpoint caches, connection generations, and pending trust prompts therefore cannot
cross the switch. Profile removal is confirmed and recoverable under `RemovedProfiles`.

Account ID and device Peer ID remain separate fields so several devices per account can be added
later. The current profile owns one protected account session and one device identity; multi-device
synchronization and automatic trust transfer are not implemented.

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

## Storage, migration, reset, and backup

Each stable profile under LocalAppData contains identity, DPAPI-protected configuration/session
state, trusted contacts, encrypted SQLite history/WAL/SHM, outbox, message metadata, and unread
state. Installation metadata contains profile IDs/names/last-used state, but no keys or messages.

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
It never enables Public-network access. Firewall/TCP diagnostics remain separate from TLS and
identity failures.

## Repository and release layout

`desktop`, `server`, and `shared` contain managed source; root C++ files remain where active
build scripts expect them. `tests`, `docs`, and `scripts` contain their named support files.
`build\v0.3-beta` is replaceable output, `releases\v0.3-beta` is the active package, and
`releases\v0.1-beta` is the immutable fallback. `shared/VersionInfo.cs` centralizes the version.
