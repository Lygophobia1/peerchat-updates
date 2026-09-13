# PEERCHAT — DEVELOPMENT GOAL

> Documentation authority
>
> - Product goals and roadmap: `PEERCHAT_DEVELOPMENT_GOAL.md`
> - Current implementation status: `PROJECT_STATUS.md`
> - Current system architecture: `ARCHITECTURE.md`
> - Discovery protocol contract: `docs/DISCOVERY_PROTOCOL.md`

## CURRENT VERSION

**Source metadata: 4.0.0-dev.2; user-accepted product baseline: v4.0.0.**

Acceptance does not imply a signed stable artifact or completed Release publication.

## CURRENT PHASE

**Stage 3 direct-session continuity ownership implemented; Stage 4 not started.**

The accepted source is frozen on `release/v4.0.0`; incremental networking work belongs on
`refactor/session-transport`. Formal Release versioning, signing backup and publication remain
separate work. Stage 3 now centralizes direct-transport winner selection, retry/backoff, cancellation/lifetime
tokens, and revisioned session snapshots in the native session/transport boundary. Durable delivery,
ACK ownership, draining, and resend behavior remain unchanged and belong to a separately authorized
Stage 4.

Milestone 1 — Account + Discovery Foundation is complete and has passed real packaged-client testing.

PeerChat is a Windows-first, privacy-focused communications application. Preserve working components whenever practical rather than rewriting them without a concrete reason.

Current architecture includes:

- C# / WPF client UI
- C-compatible adapter/bridge
- C++ networking/messaging core
- C# headless PeerChat server
- C# WPF Server Admin application
- persistent cryptographic peer/device identities
- authenticated/encrypted peer connections
- encrypted local SQLite chat history
- unique message IDs
- delivery acknowledgements/states
- reconnect functionality
- manual direct P2P connection as an advanced fallback
- authenticated account/discovery service
- real-time username/friend-code discovery
- server-side presence
- clean versioned packaged releases

---

# PRODUCT PRINCIPLE

PeerChat should be easy to use like a normal messenger while preserving a privacy-first architecture.

Public PeerChat infrastructure should help users:

**authenticate → find friends → establish connections**

Private native conversations should remain:

**Peer ↔ Peer**

whenever practical.

If direct P2P is impossible, an encrypted relay may transport traffic, but the relay must not require access to plaintext private messages.

The public server must not become the authoritative repository for private chat history.

PeerChat must remain useful even if the public PeerChat service is unavailable.

The public service is:

**server-assisted, not server-dependent**

If the service is unavailable, users should still be able to:

- launch PeerChat
- access locally encrypted conversation history
- use known peers where possible
- manually establish direct P2P connections through Advanced settings

---

# TARGET USER EXPERIENCE

A normal user should eventually be able to:

1. Install PeerChat.
2. Launch the application and choose a local profile.
3. Create/sign in to the account bound to a new profile, or automatically restore the account already bound to an existing profile.
4. Open the local profile even when the public service is unavailable, using Offline / Direct P2P mode.
5. See friends and online/offline status.
6. Search for another user by username or friend code.
7. Send a friend request.
8. Receive and Accept/Reject requests inside PeerChat.
9. Click a friend.
10. PeerChat automatically determines how to reach them.
11. Attempt a direct encrypted P2P connection.
12. Automatically perform NAT traversal where necessary.
13. Automatically use an encrypted relay if direct connectivity cannot be established.
14. Begin chatting without entering an IP address, selecting a port, or configuring a router.

Manual direct connection remains available under:

**Settings → Advanced → Direct P2P Connection**

for troubleshooting, development, and server-independent fallback use.

Voice diagnostics remain local and privacy-safe: current/recent call state, selected device labels,
media counters, and errors may be copied for troubleshooting, but audio content and secret key or
session material must never be recorded or included.
Direct voice may use standard STUN coordination while retaining host candidates for LAN/offline
operation. TURN/privacy relay remains a future fallback; neither service owns call identity,
signaling authority, or media content.
Device errors must not invalidate the logical friend/session. Invite eligibility belongs to stable
friend accounts, independent of reachability. Reliability candidates are kept separate from the
existing release until automated checks and physical acceptance establish their status.

---

# ACCOUNT AND IDENTITY MODEL

Keep these concepts separate:

## PeerChat Account

Used for:

- login
- username/friend code
- friend relationships
- discovery
- presence
- future device registration

## Local Profile

A stable local workspace selected before native-core startup. Its internal ID and directory do
not change when an account username changes. A local profile exclusively owns its device Peer ID,
private key, trust, encrypted history, contacts, endpoints, unread/pending UI state, relevant
settings, discovery/friend cache, one associated server account ID, and a DPAPI-protected renewable
session credential. Different local profiles do not share a private device identity or account
session by default. Normal account switching is profile switching. A linked profile must never
silently authenticate as another account; relinking, if added later, requires a distinct confirmed
operation. Signing out invalidates only that profile's server session and leaves its local identity,
trust, groups, and encrypted history available.

## Cryptographic Device / Peer Identity

Used for:

- authenticating devices/peers
- secure conversations
- trust verification

## Encrypted Local History

Used for:

- storing private conversations locally

Do NOT use the account password itself as a private-chat encryption key.

Do NOT upload private peer/device identity keys to the server.

A username alone is not proof of cryptographic identity.

IP addresses and ports are connection information, NOT identity.

The relationship is: an **account** is a social/server identity, a **local profile** is one
installation's account workspace, a **device Peer ID** is cryptographic identity, **trust** is an
explicit account/device association, an **endpoint** is only a temporary route candidate, and the
**server** coordinates discovery but is not required to open local state or use Direct P2P.

Design the account/device relationship so multiple authorized devices per account can be supported later without replacing the identity model.

Do NOT implement full multi-device synchronization yet.

Use established password/authentication libraries and approaches. Do not invent custom password hashing or cryptography.

---

# PUBLIC PEERCHAT SERVER

Initially the PeerChat server is hosted on the developer's own Windows PC for development/testing.

Keep deployment simple.

Prefer:

- one practical headless server executable/service
- one separate Server Admin GUI
- clear configuration
- minimal dependencies
- a database appropriate for current scale
- readable bounded logs
- straightforward startup/shutdown

Architect it so the same service can later move to an inexpensive VPS/cloud host without redesigning the client protocol.

The server may contain logically separate modules for:

- authentication
- accounts
- friends
- presence
- discovery
- rendezvous
- NAT traversal coordination
- relay
- client update metadata

These may initially run together. Do not create unnecessary microservices.

Current server runtime data belongs under:

`C:\ProgramData\PeerChat\Server\`

with organized Config, Data, and Logs locations.

Development TLS private-key material remains protected by the Windows certificate store. Configuration stores certificate identity/thumbprint information, not private key material.

---

# FRIEND SYSTEM — PRESERVED REQUIREMENTS

PeerChat should support:

- Find Friends
- username/friend-code search
- real-time prefix suggestions
- Send Friend Request
- Incoming Requests
- Accept
- Reject
- Cancel Pending Request
- persistent friendship
- Friends list
- Remove Friend
- Block
- Unblock

Friend relationships must be server-backed and associated with authenticated PeerChat accounts and persistent public cryptographic identities.

Friend-request operations require an authenticated session.

A client must not be able to forge a friend request as another account merely by knowing that account's username or Peer ID.

Handle:

- duplicate friend requests
- simultaneous/crossed friend requests
- requests to existing friends
- requests to blocked users
- self-friend requests
- rejected requests
- cancelled outgoing requests
- application restart
- server restart
- reconnect
- offline recipient

An offline user should receive pending requests after signing in later.

A friendship should survive client and server restart.

Removing a friend must NOT automatically:

- delete local conversation history
- delete the other person's PeerChat account
- delete the local cryptographic identity

History deletion is a separate explicit action.

Blocking should prevent new friend/contact requests and future server-assisted contact as appropriate.

Friend acceptance is a social/account relationship, not permission to silently replace cryptographic identity.

If a friend's cryptographic identity changes unexpectedly, PeerChat must still warn about the change.

A direct-message conversation is keyed by the stable friend/account identity. Current and previous
device Peer IDs may attach to that conversation, but endpoints, sessions, presence, and device changes
must not create duplicate social threads. Offline and live history must reconcile into that same view,
while a new device identity still requires explicit verification.

The existing modern WPF friend-search/request UI should be wired to authoritative server state rather than replaced.

Do not display successful friend state unless the server operation actually succeeded.

---

# FIND FRIENDS SEARCH

Find Friends should behave like a real-time autocomplete search.

Requirements:

- update results as the user types
- short debounce around 250–350 ms
- partial/prefix username matching
- case-insensitive search
- indexed/efficient server query
- limit results to a small reasonable number
- ignore/cancel stale searches
- exclude the signed-in user
- return only public discovery information
- respect future discoverability/privacy settings

Suggested public result fields may include:

- display name
- username/friend code
- online/offline status
- appropriate public account/identity reference

Do not expose private keys, authentication secrets, or unnecessary device details.

---

# CONNECTION / NAT / RELAY STRATEGY

PeerChat should automatically determine the best available connection path.

Choose permitted routes by channel capabilities and policy, not a universal LAN → public → STUN →
TURN ladder. Preserve current direct TCP messaging and library-managed WebRTC/ICE media separately.
STUN success for media is not TCP reachability proof. Future router mapping or relay support needs
its own demonstrated requirement; TURN is an ICE relay capability, not a separate media stack.

Apply privacy/security constraints before gathering candidates, DNS/probing, address advertisement
and fallback. Direct is preferred in Automatic mode; strict privacy may require relay-only behavior
and must fail closed rather than silently disclose a direct address. No new transport is authorized
by this roadmap.

Ordinary users must NOT need manual port forwarding.

Use mature networking standards/libraries where appropriate.

Do not invent custom NAT traversal mechanisms when established solutions exist.

Direct P2P remains available and preferred when policy permits it; relay is fallback or an explicit
privacy choice. Existing manual/server-independent operation must remain intact.

Relay infrastructure must not require plaintext private-message access.

A public IP address is connection information, not Peer ID.

PeerChat should eventually support multiple connection candidates per peer, such as:

- LAN endpoint
- public endpoint
- NAT traversal candidate
- relay candidate

The PeerChat discovery/rendezvous infrastructure should eventually determine observed public endpoints rather than relying on arbitrary third-party "what is my IP" services as permanent architecture.

---

# MESSAGE TRANSPORT / ROUTE VISIBILITY

PeerChat should know how each outgoing message was actually transported.

Users should be able to determine whether a message was delivered:

**Direct P2P**

or:

**Encrypted Relay**

Do not label a message as relayed merely because the account/discovery/rendezvous server helped peers find each other.

Conceptually distinguish:

## Direct LAN P2P

Message payload traveled directly between peers on the local network.

## Direct Internet P2P

Message payload traveled directly between peers across the Internet.

## Encrypted Relay

Direct connectivity was unavailable and the encrypted payload passed through PeerChat relay infrastructure.

The normal UI may simplify this to:

- `Direct`
- `Relayed`

with more detail available through message info/tooltips.

Example:

`✓✓ Delivered · Direct`

or:

`✓✓ Delivered · Relayed`

Preserve transport route as message/delivery metadata rather than deriving it later from current connection state.

If a retry changes transport, record the successful delivery transport while preserving useful safe diagnostics about failed attempts where appropriate.

Do not expose private keys, relay secrets, or unnecessary IP information merely to display route status.

The normal message bubble should remain uncluttered.

Preserve existing delivery metadata during migration. Do not implement relay transport in this phase.

---

# MESSAGE ORDERING — PRESERVED INVARIANT

Deterministic logical ordering is implemented. Preserve it across retries, history paging and
future handoff; do not reopen the former ordering defect without new evidence.

Do not fix this with a superficial WPF timestamp sort.

Do NOT assume sender wall clocks are synchronized.

Do not use `sent_at` alone as authoritative conversation ordering.

Do not use each client's local `received_at` timestamp as the sole ordering mechanism either.

Preserve the current logical ordering and deterministic message-ID tie breaking rather than
introducing a new ordering algorithm to fit transport interfaces.

Avoid unnecessary distributed-system complexity.

Preserve useful timestamps separately:

- sender-created/sent time
- local receive time
- future opened/read time
- future expiration information

Ordering must handle:

- rapid consecutive messages
- simultaneous sends
- mismatched system clocks
- delayed packets
- reconnects
- offline/queued messages
- retries
- duplicate delivery
- history reload after restart

A retried/duplicate message must retain its original logical position.

After restart, encrypted stored history must reconstruct into the same deterministic order.

Document the final ordering rules in `ARCHITECTURE.md`.

Add regression tests for these conditions.

---

# CONNECTION STATE AND TRUST UX

PeerChat must clearly represent connection progress.

States may include:

- Finding peer…
- Connecting…
- Establishing secure connection…
- Authenticating peer…
- Waiting for verification…
- Ready
- Connection failed
- Retrying…

Never leave the composer appearing enabled while messaging is unavailable.

If messaging is unavailable, disable it intentionally and explain why.

Examples:

**Waiting for secure connection…**

**Verify this peer before messaging.**

When verification is required, surface the trust/fingerprint UI immediately and visibly.

Persist trusted identities appropriately.

An unchanged trusted peer should not unnecessarily require repeated verification.

A changed cryptographic identity must produce a clear warning rather than silently replacing the trusted identity.

Normal account/login UX should not require users to manually copy/paste server certificate fingerprints.

Development/self-hosted trust may use an explicit first-use trust flow.

Production should eventually use a stable service hostname and normal trusted TLS certificate validation.

---

# PRIVACY MODEL

Privacy is a primary PeerChat design goal.

Native private conversations should remain end-to-end authenticated/encrypted.

Public infrastructure should not require plaintext private-message access to provide:

- login
- friends
- presence
- discovery
- rendezvous
- NAT traversal
- relay
- updates

Chat history remains local-first and encrypted at rest.

Avoid plaintext private-message content in:

- logs
- temporary files
- debug output
- crash reports
- search indexes

Minimize server-side metadata collection and retention.

Document:

- what important metadata is stored
- why it is needed
- how it is protected
- how long it is retained

---

# FUTURE DISAPPEARING / EPHEMERAL MESSAGES

Once connectivity is reliable, PeerChat will support disappearing messages.

Do NOT implement the full feature during the current milestone.

Current message/protocol/storage decisions must not unnecessarily prevent it.

Future policies should support:

- persistent
- delete after opening
- delete after delivery
- absolute expiration
- maximum unopened lifetime
- conversation-wide disappearing-message defaults

The message lifecycle should eventually support:

**Sent → Delivered → Opened → Expired**

Expiration must be a protocol/storage concept, not merely a UI timer.

Ephemeral content should eventually be removable from:

- encrypted history
- attachments
- caches
- indexes
- notification data where applicable

Disappearing messages reduce retained data but cannot guarantee that a recipient did not screenshot, copy, photograph, record, or otherwise preserve legitimately displayed content.

---

# GROUPS AND MEDIA DIRECTION

PeerChat now has a client-owned, peer-replicated group-text foundation. Group IDs, names, metadata,
membership, roles, invitations, removals, leave state, and history are signed client state; the
discovery service is not authoritative for any of them. Groups must continue over direct P2P when
the service is unavailable. A future optional relay may carry the same encrypted envelopes without
changing the logical group or its authority.

Group membership is an arbitrary collection. Three clients are the minimum acceptance test, never
a product or architecture limit. Any future practical limit must be configurable policy rather than
a hard-coded schema, protocol, storage, routing, authorization, or UI assumption.

One-to-one voice is implemented through WPF → C ABI → native C/C++, using the pinned WebRTC/Opus
stack. Prove messaging and voice continuity through the planned abstraction before adding media
features. Later directions still include:

- group voice
- screen sharing
- webcam video
- file transfer

Do not begin group voice, screen sharing, webcam video, or file transfer during the current phase.

Avoid architectural assumptions that permanently restrict conversations to exactly two participants.

Continue using established codecs/protocols/libraries for later media work.

Do not force reliable ordered text semantics onto latency-sensitive media simply to reuse existing code.

---

# PLANNED CLIENT ARCHITECTURE / REFACTOR DIRECTION

> Planned direction, not the currently implemented architecture. Preserve proven networking and
> security behavior while responsibilities are migrated incrementally and verified.

PeerChat will move toward a transport-independent `PeerSession` scoped to one local profile and
verified remote device. Social/account conversation identity remains separate from cryptographic
device identity, runtime session epoch and route. Changing devices still requires explicit trust.

`SessionTransportManager` will solely own transport attempts, promotion, draining, retry and
cancellation, using active bindings per channel/channel bundle. Control/messages and media may
use different bindings. `TransportRouter` ranks immutable observations under policy; it owns no
sockets, timers, discovery credentials or delivery state. Durable outbox/ACK/resend/dedup belongs
in a separate delivery module above transports, with destination/message-scoped ACK eligibility
that survives permitted draining. Group ledger authority remains client-owned and separate.

Future route changes should use make-before-break handoff where possible:

**establish alternate route → authenticate and verify the expected peer → confirm ready → promote
the new binding → drain and retire the old route**

Promotion is locally atomic, not distributed atomicity. Authenticated, capability-gated peer
coordination and legacy fallback must precede intentional overlap. Candidate failure must preserve
a healthy winner. Lifetime/instance tokens reject stale callbacks; route hints or presence changes
trigger reevaluation, not automatic teardown. Call lifetime, control availability and media
availability remain distinct. WPF will consume authoritative revisioned snapshots instead of owning
reconnect, winner or disconnect decisions. Maintain exactly one production decision owner while
each responsibility migrates.

Keep peer/account identity separate from endpoint/route identity. Messaging, voice, and future
screen sharing, video, and file transfer should consume the shared session/channel abstraction
instead of creating separate reconnect stacks. Feature-specific recovery should remain small—for
example, requesting delivery recovery through the shared module, resuming a file from an acknowledged
offset, or requesting a media keyframe after handoff. Reliable-message and realtime-media semantics
remain distinct; libraries retain ICE, congestion, retransmission and jitter machinery.

New features must not add substantial reconnect, route-selection, stale-route, winner-selection, or
identity-verification logic of their own. New orchestration should use narrow controllers/services
instead of continuing to grow `App.cs`; native functionality should be wrapped by adapters/services
rather than scattered direct P/Invoke calls throughout WPF. Keep Windows-specific behavior behind
platform adapters where practical so the portable core remains suitable for future Android reuse.
Use portable UTF-8/fixed-width contracts, explicit buffer/callback lifetimes and opaque platform-key
handles; preserve Windows implementations, existing ciphertext and identity-key handling.

Separate materially different state, lifecycle, concurrency and test responsibilities into focused
classes/files: session, transport ownership, delivery, routing/policy, call/media coordination and
platform adapters. Prefer composition over another large orchestrator. Interfaces/factories need
real ownership, testing, portability or substitution value; no ceremonial pair for every class.

The auto-updater remains self-contained and outside the session/transport abstraction. Continue
using mature libraries for TLS, WebRTC, ICE, STUN/TURN, DTLS-SRTP, Opus, and cryptographic
primitives. Do not replace the working networking stack with a new P2P framework without a
demonstrated need, and do not introduce a plugin architecture or broad rewrite before this planned
incremental refactor.

Intended sequence:

**accepted baseline/updater checkpoint → types and direct wrapper without behavior changes →
single winner/retry/lifetime owner → delivery continuity → capability-gated direct handoff →
voice control migration → media continuity proof → packaged physical acceptance → retire proven
legacy paths → client modularization → screen sharing → video/files**

Use incremental engineering checkpoints and separate physical-acceptance milestones. Full regression
belongs at significant checkpoints; local and affected integration gates cover smaller stages.

---

# FUTURE DISCORD / EXTERNAL SERVICE BRIDGE

PeerChat is intended to eventually support Discord interoperability where supported Discord APIs permit it.

Use an official/supported bot/application/bridge architecture.

Do NOT automate normal Discord user accounts or rely on unsupported self-bot behavior.

Do NOT implement Discord integration now.

Keep external bridges modular and separate from native P2P networking.

Future bridged messages must be visibly identifiable as external.

Do not show PeerChat's native E2E privacy indicator for content that an external service necessarily receives.

---

# AUTOMATIC CLIENT UPDATE SERVICE

PeerChat has an isolated updater and client handoff with Development publishing and a fail-closed
Release verifier. Formal stable version/artifact selection, signing-key backup and Release
publication remain separate from the networking migration; current evidence is in PROJECT_STATUS.md.
Public infrastructure should provide automatic update information so new builds do not need to be
manually distributed.

Support:

- installed/current version
- latest version
- minimum supported version
- release notes
- download location
- package size/hash
- cryptographic signature
- recommended updates
- mandatory security updates where appropriate

The client must NEVER trust an executable solely because the public server says it is an update.

Updates must be cryptographically signed and independently verified by the client using a trusted update-signing public key.

Release must remain fail-closed through the configured verifier and trusted public key. An explicitly
authorized Development-only unsigned package may use the existing opt-in boundary, but must still
use HTTPS plus exact size and SHA-256 verification.

Compromise of the account/discovery server alone must not be sufficient to distribute trusted malicious executables.

Keep the private update-signing key off the public production server.

Design updates to be rollback-safe where practical.

Keep update delivery logically separate so binaries can later move to static hosting/CDN/object storage.

---

# BACKGROUND PERFORMANCE REQUIREMENTS

PeerChat is intended to remain running while the user is gaming or performing demanding tasks.

CPU, GPU, memory, disk, and network efficiency are product requirements.

Prefer event-driven/asynchronous behavior over busy polling.

Avoid:

- busy-wait loops
- high-frequency UI/native polling when event/wait mechanisms are practical
- unnecessary database polling
- aggressive reconnect loops
- unnecessary serialization/reloads
- unnecessary disk writes
- excessive logs
- continuous redraw/animation while minimized or hidden

Use bounded exponential retry backoff with jitter. Authoritative candidate/presence changes and an
explicit user retry may bypass the current delay, but neither may reset or bypass identity trust.

The headless server should not use GPU resources.

The client should use negligible GPU resources while minimized/backgrounded.

Measure performance before rewriting working C++ or C# components merely for theoretical efficiency.

The current C# server is acceptable.

The existing C++ client backend should remain unless profiling demonstrates a concrete reason to change it.

---

# SERVER AND PROTOCOL SECURITY

Treat the PeerChat server as Internet-facing software.

Include reasonable protections against:

- malformed requests
- oversized payloads
- invalid protocol versions
- spoofed identities
- duplicate requests
- authentication brute force
- resource exhaustion
- unsafe database input
- invalid sessions/tokens

Use established cryptographic/authentication libraries.

Maintain protocol versioning/capability information where appropriate.

Future clients will gain features. Avoid protocols that require every version to understand every future optional field.

---

# CODE CLARITY AND MAINTAINABILITY — REQUIRED

I personally intend to understand, troubleshoot, modify, and maintain PeerChat.

**Readable code is a project requirement, not optional polish.**

This applies equally to:

- C++ networking/messaging core
- C# WPF client
- adapter layer
- PeerChat server
- Server Admin
- authentication
- database/storage
- protocol implementation
- cryptographic integration
- rendezvous
- NAT traversal
- relay
- updater

Use descriptive names for:

- classes
- functions
- methods
- variables
- interfaces
- protocol messages
- connection states
- database tables/columns
- events

Avoid cryptic abbreviations except established technical terminology.

Add comments explaining **WHY** non-obvious code works the way it does.

Comments are particularly important around:

- authentication
- password handling
- cryptographic identity
- account/device relationships
- trust verification
- encryption/TLS
- threading/concurrency
- socket lifetime/ownership
- connection-state transitions
- reconnect behavior
- message ordering
- logical sequence/order handling
- duplicate handling
- acknowledgements
- message transport route
- rendezvous
- NAT traversal
- relay behavior
- database transactions
- encrypted storage
- disappearing-message lifecycle
- update verification
- security boundaries
- Windows-specific behavior

Do not add comments that merely repeat obvious statements.

Prefer comments explaining:

**assumptions → ownership → security requirements → edge cases → failure behavior → reason for design**

Major server modules should contain a concise description of:

**Purpose → Inputs → Outputs → Security assumptions → Ownership/lifetime**

Do not sacrifice readability merely to reduce line count.

---

# DOCUMENTATION

Maintain `ARCHITECTURE.md` covering:

- startup
- login/authentication
- account storage
- account/device identity mapping
- listener/outbound connection
- friends
- presence
- discovery
- rendezvous
- NAT traversal
- relay
- direct P2P fallback
- peer authentication/trust
- message send/receive
- message ordering
- transport-route metadata
- acknowledgements
- encryption/decryption boundaries
- encrypted local history
- server database
- update system
- what the server can see
- what the server cannot see

Maintain `PROJECT_STATUS.md` with:

- current version
- current milestone
- what works
- current blocker
- next action
- important architectural decisions
- tests completed
- known bugs
- deferred work
- exact current packaged executable paths

---

# RELEASE / BUILD STRUCTURE

Known-good fallback release:

`releases\v0.1-beta\`

Retained v0.3 package layout (not the accepted v4 candidate):

- `releases\v0.3-beta\Client\PeerChat.Desktop.exe`
- `releases\v0.3-beta\Server\PeerChat.Server.Admin.exe`
- `releases\v0.3-beta\Server\PeerChat.Server.exe`

Temporary compiler output belongs under:

`build\v0.3-beta\`

The accepted retained client is `releases\v4-dev.2-history-paging-runtimefix\Client`; exact package
selection and acceptance belong in PROJECT_STATUS.md. Stage new verification output separately;
never overwrite accepted artifacts or assume a rebuild has identical bytes. Preserve historical
packages and evidence. Do not manually clean historical source files.

Packaged clients must be self-contained: native third-party/toolchain runtime dependencies are
statically linked where appropriate or shipped beside the executable, and release verification must
not rely on compiler installations or development-machine `PATH` entries.

Future versions should continue this versioned release approach.

---

# DEVELOPMENT MILESTONES

The numbered milestones below retain product acceptance requirements, not a new execution order.
Milestones 1–3 are implemented; direct ICE/STUN voice and updater integration also exist. Remaining
NAT/relay, formal Release and future feature work follow the approved migration sequence above,
not stale imperative wording in historical milestone checklists.

Work incrementally.

Do NOT attempt every milestone simultaneously.

## MILESTONE 1 — ACCOUNT + DISCOVERY FOUNDATION — COMPLETE

Milestone 1 has passed real packaged-client validation.

Two separate PeerChat clients can:

- connect to the PeerChat development server
- create/authenticate accounts
- register persistent public device identities
- restore authenticated sessions
- report presence
- search usernames/friend codes
- receive real-time search suggestions
- find the correct remote account/public identity

Preserve this functionality and regression coverage.

Do not reopen Milestone 1 architecture unless Milestone 2 exposes a concrete defect.

## MILESTONE 2 — FRIEND SYSTEM — COMPLETE

Implement the existing Milestone 2 UI as real authenticated server-backed functionality.

Complete:

**Find user**
→ **Send Friend Request**
→ **remote client receives request**
→ **Accept / Reject**
→ **friendship persists**
→ **both clients display friend correctly**

Implement:

- send friend request
- receive friend request
- accept
- reject
- cancel outgoing request
- persistent friendship
- friends list
- remove friend
- block/unblock
- online/offline state
- offline pending-request recovery
- restart/reconnect persistence

Use the existing modern WPF UI.

Replace presentation-only/local scaffold states such as `Request Sent` with authoritative server-backed state.

Add appropriate database schema/indexes and authenticated versioned friend operations.

Use parameterized/transactional database operations where appropriate.

Test with two real packaged WPF clients.

## MILESTONE 3 — AUTOMATIC DIRECT CONNECTION + MESSAGE ORDERING — COMPLETE

Once accounts are friends, use rendezvous information to automatically establish the existing secure P2P connection.

No manual IP entry for normal use.

Friendship provides authorization/discovery context; cryptographic identity still authenticates the actual secure peer.

During this milestone:

- fix deterministic message ordering
- integrate clear connection/trust states
- automatically connect friends
- begin recording actual successful message transport route

When direct delivery succeeds, record:

`Direct P2P`

and where useful distinguish LAN vs Internet.

Do not mark messages `Relayed` until actual relay transport exists.

Verify:

- automatic friend → peer connection
- correct live ordering
- mismatched clocks
- simultaneous sends
- delayed messages
- duplicates
- queued messages
- reconnect
- restart/history reload
- correct persisted transport metadata

## MILESTONE 4 — NAT TRAVERSAL

Establish direct connectivity across ordinary residential Internet connections without manual port forwarding whenever topology permits.

Use established methods/libraries where appropriate.

## MILESTONE 5 — ENCRYPTED RELAY FALLBACK

Automatically use encrypted relay transport when direct connectivity cannot be established.

The relay must not require plaintext message access.

When relay delivery succeeds, record:

`Encrypted Relay`

Switching between direct and relay must not:

- duplicate messages
- change message identity
- break deterministic ordering
- lose acknowledgements
- reset conversation history

If a conversation later returns from relay to direct P2P, new messages should record their actual individual route.

## MILESTONE 6 — REAL FRIEND TEST

Demonstrate:

**install → create/login → find → add → accept → automatically connect → secure correctly ordered text chat**

across two normal different home Internet connections without manual router configuration.

The user should also be able to tell whether delivered messages were:

**Direct P2P**

or:

**Encrypted Relay**

## MILESTONE 7 — AUTOMATIC UPDATE SYSTEM

Implement signed automatic updates.

Test:

- successful update
- retained account/history
- safe restart
- rejection of tampered/unsigned updates

## MILESTONE 8 — DISAPPEARING MESSAGES

Implement the disappearing-message system after connectivity/update infrastructure is sufficiently stable.

Reassess detailed requirements before implementation.

---

# CURRENT EXECUTION RULE — IMPORTANT

Everything above defines PeerChat's architectural direction.

**Milestones 1–3, scalable client-owned group text, and native one-to-one direct
WebRTC/DTLS-SRTP + Opus calls are implemented. Packaged two-way voice and 3+ client group behavior
have passed physical acceptance; exact current evidence remains in `PROJECT_STATUS.md`.**

Stage 3 of the approved session/transport migration is implemented: the existing direct path has one
native winner/retry/cancellation authority with lifetime tokens and revisioned snapshots. Do not begin
Stage 4 delivery/ACK migration until it is explicitly authorized. Networking/session migration comes
before broad client modularization. Preserve the accepted baseline and the open CLR/WPF and
false-route-teardown backlog; do not infer closure from general product acceptance.

Preserve the completed account, friend, manual P2P, TLS identity, firewall, notification, and
encrypted-history behavior. Current implementation details and test state belong in
`PROJECT_STATUS.md`; protocol fields belong in `docs/DISCOVERY_PROTOCOL.md`.

Do NOT begin:

- additional NAT traversal or TURN/relay work beyond the implemented direct ICE/STUN path
- relay implementation
- changes to the independent updater or Release signing/publication workflow in this refactor
- client modularization before session/messaging/voice continuity is accepted
- disappearing messages
- Discord bridge
- video
- screen sharing
- file transfer
- multi-device synchronization

Preserve:

- existing direct manual P2P fallback
- encrypted local history
- account/discovery functionality
- cryptographic peer identities
- frozen baseline branches, v0.1-beta fallback and retained accepted packages
- modern WPF UI
- Server Admin/headless-server separation

Keep group state client-owned and transport-independent. Preserve the verified voice device,
ringing/accept/reject/end, mute/deafen, failure, profile-switch teardown, and identity boundaries.
Voice device controls and read-only input/output meters should use the existing dark UI theme.
Level telemetry must be derived from PCM already crossing the native media path, expose only small
normalized values/state to WPF, reset with the call/profile lifecycle, and never retain audio.

Keep everyday conversation actions header-driven and the desktop visually flat. Active-call controls
belong in a slim conversation-header strip; engineering diagnostics stay collapsed, height-capped,
and internally scrollable inside that call area. The right sidebar remains a compact global Security
& Status surface. DM details and group members, invites, settings, rename/leave, and applicable
member administration open from anchored conversation-header actions instead of replacing or
stacking cards in the global sidebar. The local profile chooser should match the dark client, keep
creation progressive, and place rename/delete behind the selected profile's overflow without changing
bootstrap or isolation semantics.

---

# BUILD / TEST / EXECUTION RULE

Keep PeerChat runnable.

Build and test frequently.

Do not accumulate a huge untested rewrite.

Preserve working backend functionality unless a concrete architecture/security requirement necessitates changing it.

Do not claim something works merely because it compiles.

Test through actual packaged applications where practical.

When a milestone is complete:

1. Run milestone tests.
2. Run relevant regression tests.
3. Test real packaged clients.
4. Update `PROJECT_STATUS.md`.
5. Update `ARCHITECTURE.md` when appropriate.
6. Record known problems.
7. Clearly report what was completed.
8. Report the gate and remaining physical evidence; continue only within the user-authorized stage.

---

# DEFINITION OF SUCCESS FOR THE CONNECTIVITY PHASE

A user can give PeerChat to a friend on another normal home Internet connection and they can:

**install → create/login → find → add → accept → PeerChat automatically connects them → correctly ordered encrypted text chat**

without either person:

- exchanging IP addresses
- manually entering ports
- configuring port forwarding
- understanding NAT

Direct P2P is preferred.

Automatic NAT traversal is used when needed.

Encrypted relay is fallback.

Conversation history remains locally encrypted.

Messages remain deterministically ordered even when clocks differ or delivery is delayed.

PeerChat records whether each delivered message traveled:

**Direct P2P**

or:

**Encrypted Relay**

Direct/manual P2P remains available as an advanced server-independent fallback.

Once this is reliable, proceed through later milestones rather than randomly jumping between future features.
