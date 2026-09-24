# PeerChat discovery protocol

> Documentation authority
>
> - Product goals and roadmap: `PEERCHAT_DEVELOPMENT_GOAL.md`
> - Current implementation status: `PROJECT_STATUS.md`
> - Current system architecture: `ARCHITECTURE.md`
> - Discovery protocol contract: `docs/DISCOVERY_PROTOCOL.md`

## Transport and framing

This document specifies the existing discovery wire/API and authority boundaries. Client scheduling,
interface enumeration and invalidation implementation live in ARCHITECTURE.md. The approved
session/transport plan introduces no discovery operation or field here.

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
- `directCandidates`: optional Boolean capability request. Omitted means legacy direct-candidate
  presence; `false` creates valid relay-only presence with no direct endpoint.
- `endpointHost`, `endpointPort`: optional LAN/private P2P candidate when `directCandidates` is true
- `publicEndpointHost`, `publicEndpointPort`: optional explicit authenticated public candidate when
  `directCandidates` is true

The server advertises `relayOnlyPresence: true` in login/register/session-info responses. A client
that needs relay-only presence must require that capability rather than sending a privacy-sensitive
endpoint to an older server. Legacy version-1 clients omit `directCandidates` and retain their
existing direct-presence behavior.

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
the current client's heartbeat scheduling is documented in ARCHITECTURE.md.

Candidates are route hints, never identities. Relay-only presence remains online and relay-reachable
with `endpointHost`, `publicEndpointHost`, and both ports empty/zero; ordinary friend snapshots and
presence events therefore disclose no direct route for that account. A future mutually-consented,
peer-scoped P2P authorization may exchange short-lived direct candidates separately. Candidates
must still complete the native mutual-TLS Peer ID verification.

Presence is advisory reachability information, not authenticated P2P liveness. An `online=false`
snapshot, expired observation or disappearing candidate is not a server instruction to close a
healthy authenticated connection. Such changes are observations for client reevaluation. They grant
neither trust nor permission to rebind a social account to a different device. Candidate freshness
does not replace mutual-TLS identity verification. Client route/retry decisions and their current
limitations are documented in ARCHITECTURE.md, not dictated by this wire contract.

## Event stream

`events` authenticates once, returns `events_ready`, and keeps the TLS socket open. The server
sends `friend_event` frames for friend requests/updates, friendship changes, and actual presence
changes. Repeated identical heartbeats do not emit presence events.

Events notify clients to reconcile authoritative friend/account state; they are not a second source
of truth. Client snapshot, heartbeat and event-reconnect timing is implementation policy documented
in ARCHITECTURE.md. Discovery unavailability does not command P2P session teardown.

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
Group invite eligibility, pending-state projection and signed-event validation remain client behavior
documented in ARCHITECTURE.md; discovery results alone never authorize a group-state change.

Group state is a client-owned signed event ledger replicated between members. Existing groups and
their direct text transport continue when this service is unavailable. The implemented reliable
relay may carry the same inner peer-TLS application stream for `Automatic — prefer P2P, relay
fallback`; `Direct Only` remains available. Relay acceptance, ordering, storage, or availability
does not turn the service into the source of truth or change the logical group.

## Opaque reliable relay channel

`relay_attach` is sent as one ordinary framed JSON request on a dedicated certificate-pinned server
TLS connection. It contains the authenticated `sessionToken`, the caller's registered
`publicIdentity`, `targetAccountId`, and exact `targetPublicIdentity`. Authorization requires both
device IDs to belong to their authenticated accounts and either an established friendship or one
current pending friend request between the accounts. Pending authorization exists only so the inner
peer TLS exchange can establish/verify Peer ID; server authorization itself grants no peer trust.

Both exact account/device directions must attach reciprocally. The server rejects unrelated users,
device/account mismatches, duplicate attachment and cross-profile reuse. Pairing expires after 15
seconds. A matched pair receives `relay_ready` with one random 256-bit base64url session ID and
opposite `incoming` roles. The session ID is 43 ASCII characters and is never a peer identity.

After `relay_ready`, each outer frame is `session-id || opaque-inner-peer-TLS-bytes`. JSON discovery
messages are not valid relay frames and relay bytes are never processed by the account dispatcher.
The server verifies the session prefix and caps opaque payloads at 64 KiB before synchronously
forwarding the unchanged frame to the reciprocal authenticated attachment. It does not persist or
log payloads and holds no unbounded relay queue. An authenticated matched channel may remain idle;
clients must not apply the short discovery-request receive timeout to its long-lived read. Closing
either attachment, including an explicit client shutdown close, closes the pair.

The existing peer TLS certificate/Peer ID, expected-identity check, explicit trust, application
framing, delivery ACK authorization and deduplication remain inside this stream and authoritative.
Voice/media are not routed by this channel. Relay retry/fallback/promotion policy is client-side
`SessionTransportManager` state, not a server operation.

## Voice signaling boundary

The discovery/account protocol does not own calls and stores no call state, voice diagnostics, or
voice content. Local bounded voice diagnostics and normalized live level telemetry are never uploaded
by this protocol. Current call SDP/ICE signaling travels inside the verified peer-TLS control channel,
which may use a direct transport or the opaque reliable relay; the discovery API does not interpret
those frames and audio/media never uses that relay. Discovery loss does not itself terminate a call. Current call teardown and media
implementation are described in ARCHITECTURE.md; they are not discovery protocol commands.

Future ICE/TURN signaling may use coordination infrastructure, but it must remain authenticated to
the same account/device trust relationship and must not make the server a call authority or content
archive.
STUN/TURN coordination is separate from social discovery. TURN remains unimplemented. Future peer
handoff proposals, arbitration, promotion and channel authority must remain authenticated peer/client
operations, not discovery authority. No handoff wire messages or new server operations are added here.

## Scoped reliable P2P authorization

`p2p_request` and `p2p_consent` use the authenticated discovery event channel, not the opaque inner
peer-TLS relay stream. A request ID is a random 256-bit base64url token, memory-only, bound to the
initiator/recipient account and exact registered device identities, purpose `reliable P2P probe`,
and a 60-second expiry. The server requires an established friendship and replaces a duplicate
request for the same exact pair.

The recipient receives the request without any endpoint. Only after explicit acceptance may either
exact participant submit one temporary `p2p_candidates` endpoint. The server releases the opposite
endpoint only after both submissions. Wrong account/device/token, pre-consent submission, expiry,
friendship removal, blocking, logout, cancellation, and replay fail closed. These endpoints never
enter presence, `friend_state`, ordinary events, or persistent server storage. Consent is route
permission only; subsequent native direct TLS must still pass Peer-ID and trust checks.

`p2p_request`, `p2p_accepted`, `p2p_declined`, `p2p_candidates_ready`, and `p2p_cancelled` are
authenticated discovery-event notifications. The first four consent/authorization notifications
carry no endpoint; `p2p_candidates_ready` carries only the recipient-specific temporary peer
endpoint, request ID, peer identity, and expiry. A client must treat the expiry as a wall-clock
authorization boundary; the native transport converts it to its own monotonic deadline before
admitting the probe. The candidate submitter must consume its authenticated response as well as the
other participant's event, so both peers receive their opposite route exactly once.

## Security and privacy

The service never receives private device keys, plaintext chat messages, encrypted history,
message keys, or peer TLS secrets. Operational logs may include account ID/username, public device
identity, candidate addresses, classification/source, and state transitions. They must not include
passwords, session tokens, private keys, message plaintext, or TLS secrets.
