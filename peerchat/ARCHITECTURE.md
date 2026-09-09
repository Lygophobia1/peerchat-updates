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
- **Device/Peer ID:** persistent public cryptographic identity derived from the device TLS key.
- **Local trust:** the peer fingerprint/public key explicitly accepted on this installation.
- **Endpoint candidate:** a temporary network route. An IP address is never an identity.
- **Local history:** encrypted client-side messages and delivery/order metadata.

The server receives public device identity only. Private device keys, plaintext messages, chat
keys, password plaintext, and local history never leave the client. Passwords produce salted
PBKDF2-SHA256 server verifiers and are never chat-encryption keys. Session tokens are random,
stored hashed on the server, and DPAPI-protected on the client.

A Fresh Client reset creates a new device identity. Presence registers it for the signed-in
account. Friends receive that current identity as the expected identity for a direct connection.
If TLS presents a different identity, the client shows one explicit verification prompt keyed by
expected identity, presented identity, and connection generation. Acceptance persists the new
local trust; rejection suppresses repeats for that attempt only.

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

## Storage, reset, and backup

The client profile under LocalAppData contains identity, DPAPI-protected configuration/session
state, trusted contacts, encrypted SQLite history/WAL/SHM, outbox, message metadata, and unread
state. Backup copies the complete protected profile beneath a timestamped `PeerChat` directory.
Reset stops the client and moves the complete live profile into that backup before restart, leaving
no adjacent live-profile remnants. Restore moves the current profile into an automatic safety
backup, copies the chosen profile, and restarts. Backups are not decrypted and contain no passwords.

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
