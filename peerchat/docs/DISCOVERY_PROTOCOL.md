# PeerChat discovery protocol

> Documentation authority
>
> - Product goals and roadmap: `PEERCHAT_DEVELOPMENT_GOAL.md`
> - Current implementation status: `PROJECT_STATUS.md`
> - Current system architecture: `ARCHITECTURE.md`
> - Discovery protocol contract: `docs/DISCOVERY_PROTOCOL.md`

## Transport and framing

Protocol version 1 uses TLS 1.2/1.3 and length-prefixed UTF-8 JSON. A four-byte big-endian length
precedes each request or response. Requests are limited to 4 KiB and responses to 16 KiB.
Development clients explicitly pin/confirm the service certificate; production is intended to use
normal hostname and public-CA validation.

Each request contains `version: 1` and `type`. Every operation after register/login requires a
valid `sessionToken`. Tokens are random, expire according to server configuration, and are stored
only as SHA-256 hashes on the server. Passwords appear only in register/login TLS requests and are
never logged or persisted by clients.

## Account/session operations

Client session tokens are scoped to the selected stable local profile. Profile selection and native
device-identity loading happen before any operation in this section. The client validates a restored
session with `session_info` and accepts it only when its account ID matches the profile's durable
binding. Switching closes the event channel and discards account/friend/presence runtime state before
another profile authenticates. The server never receives the local profile ID.

- `register`: `username`, `password`, `publicIdentity`; returns session token, account ID,
  normalized username, and lifetime.
- `login`: `username`, `password`; returns the same session fields.
- `session_info`: authenticated account ID and username for restoring older saved sessions.
- `logout`: revokes only the presented session token; other profile/device sessions remain valid.
- `search`: exact case-insensitive public username lookup excluding the signed-in/blocked user.
- `suggest`: case-insensitive prefix lookup, limited to 15 public profiles.

Search/suggestion results contain only public account ID, username, current public device identity,
and allowed presence state. They never return endpoint candidates.

## Friend operations

`friend_request`, `friend_respond`, `friend_cancel`, `friend_remove`,
`friend_block`, and `friend_unblock` accept a target `accountId`. The server derives the acting
account from the session token. `friend_state` returns authoritative incoming, outgoing, friends,
and blocked collections. Only the `friends` collection includes endpoint candidates.

## Presence and candidates

`presence` contains:

- `publicIdentity`: current public device/Peer identity
- `endpointHost`, `endpointPort`: LAN/private P2P candidate
- `publicEndpointHost`, `publicEndpointPort`: optional explicit authenticated public candidate

The endpoint ports are client P2P listener ports. The server discovery port is never substituted.
If a public endpoint is omitted, a valid server-observed public IPv4 is combined with the advertised
P2P listener port. Loopback, unspecified, RFC1918, link-local, shared, multicast, and reserved
observed addresses are rejected.

For a same-host client whose discovery connection is loopback, the server may use configured
`SelfHostedPublicP2PHost` only when the submitted LAN address belongs to a local server interface.
It still combines that address with the client's advertised P2P listener port.

The server binds a public device identity to its first authenticated account and rejects a presence
attempt that presents the same identity from a different account. The response echoes the authoritative account/username, registered device identity, LAN/public
candidates, `publicCandidateSource`, and `observedAddressClassification`. Friend-state candidate
records include those fields plus `candidateSeenUtc`. Presence is considered fresh for 90 seconds;
clients heartbeat every 60 seconds.

Candidates are route hints, never identities. They are released only to authenticated friends and
must still complete the native mutual-TLS Peer ID verification.

## Event stream

`events` authenticates once, returns `events_ready`, and keeps the TLS socket open. The server
sends `friend_event` frames for friend requests/updates, friendship changes, and actual presence
changes. Repeated identical heartbeats do not emit presence events.

Events notify clients to update or reconcile; they are not a second source of truth. Clients fetch
a full snapshot at login and event-channel reconnect. The bounded fallback is three minutes while
events are healthy and one minute while reconnecting. Event reconnect uses backoff and direct P2P
sessions continue independently.

The client maps each authoritative friend account to one profile-local conversation and treats the
snapshot's device Peer ID and endpoint fields as current attachments/candidates. A device change may
update that mapping only through the normal explicit peer-verification boundary; server friendship
state never grants cryptographic trust by itself.

If initial authentication, presence, or events are unavailable, the selected encrypted profile
remains open in Offline / Direct P2P mode. Manual/saved direct connections do not require this
protocol, and established direct sessions are not closed when discovery disconnects.

## Group and relay authority boundary

This discovery protocol has no authoritative group operations. It does not create or allocate Group
IDs, names, metadata, memberships, roles, invitations, removals, leave state, or history. Friend and
presence results may help a client locate a verified account/device before the client signs and sends
a group invitation directly, but that lookup does not give the service authority over the group.

Group state is a client-owned signed event ledger replicated between members. Existing groups and
their direct text transport continue when this service is unavailable. A future service version may
relay opaque encrypted group-state or message envelopes for `Automatic — prefer P2P, relay fallback`
or `Relay / Privacy Mode`; `Direct Only` remains available. Relay acceptance, ordering, storage, or
availability must not turn the service into the source of truth or change the logical group.

## Voice signaling boundary

The discovery/account protocol does not own calls and stores no call state or voice content. Once
two friends have an authenticated direct peer session, call offer/answer SDP, trickled ICE candidates,
connected, decline, busy, and end signals travel on that verified mutual-TLS channel. Each signal carries an ephemeral random
call ID and is accepted only from the already authenticated device Peer ID. One active call is
allowed per selected profile in v0.3-beta. Discovery loss does not deliberately terminate a call;
peer transport loss or profile shutdown does. Media is direct ICE with DTLS-SRTP and Opus; neither
SDP/ICE coordination nor media packets create server-owned call state.

Future ICE/TURN signaling may use coordination infrastructure, but it must remain authenticated to
the same account/device trust relationship and must not make the server a call authority or content
archive.

## Security and privacy

The service never receives private device keys, plaintext chat messages, encrypted history,
message keys, or peer TLS secrets. Operational logs may include account ID/username, public device
identity, candidate addresses, classification/source, and state transitions. They must not include
passwords, session tokens, private keys, message plaintext, or TLS secrets.
