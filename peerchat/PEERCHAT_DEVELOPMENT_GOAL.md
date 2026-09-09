# PEERCHAT — DEVELOPMENT GOAL

> Documentation authority
>
> - Product goals and roadmap: `PEERCHAT_DEVELOPMENT_GOAL.md`
> - Current implementation status: `PROJECT_STATUS.md`
> - Current system architecture: `ARCHITECTURE.md`
> - Discovery protocol contract: `docs/DISCOVERY_PROTOCOL.md`

## CURRENT VERSION

**v0.3-beta**

## CURRENT PHASE

**Milestone 3 — Automatic Direct Connection + Message Ordering**

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
2. Launch the application.
3. Create a PeerChat account or sign in.
4. Automatically connect to the PeerChat public service.
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
settings, discovery/friend cache, and protected account session. Different local profiles do not
share a private device identity by default.

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

# FRIEND SYSTEM — CURRENT MILESTONE

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

Prefer approximately:

1. existing known/direct endpoints
2. same-LAN/direct connection
3. public connection candidates
4. established NAT traversal / hole punching
5. automatic router mapping such as UPnP / NAT-PMP / PCP where useful
6. encrypted relay / TURN-like fallback

Ordinary users must NOT need manual port forwarding.

Use mature networking standards/libraries where appropriate.

Do not invent custom NAT traversal mechanisms when established solutions exist.

Direct P2P remains preferred.

Relay is fallback.

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

During Milestone 2, inexpensive forward-compatible protocol/storage/UI fields may be added for transport metadata, but do NOT implement relay transport yet.

---

# MESSAGE ORDERING — REQUIRED FIX DURING MILESTONE 3

The current text system has demonstrated a bug where messages can appear out of order.

Do not fix this with a superficial WPF timestamp sort.

Do NOT assume sender wall clocks are synchronized.

Do not use `sent_at` alone as authoritative conversation ordering.

Do not use each client's local `received_at` timestamp as the sole ordering mechanism either.

Establish deterministic P2P conversation ordering.

Choose the simplest robust approach compatible with the architecture, such as:

- per-conversation logical sequence information
- Lamport/logical ordering
- deterministic message-ID tie breaking
- another established deterministic approach

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

# FUTURE GROUPS AND MEDIA

PeerChat will eventually support:

- group conversations
- voice
- group voice
- screen sharing
- webcam video
- file transfer

Do NOT implement these during the current phase.

Avoid architectural assumptions that permanently restrict conversations to exactly two participants.

When real-time media work begins later, use established codecs/protocols/libraries.

Do not force reliable ordered text semantics onto latency-sensitive media simply to reuse existing code.

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

PeerChat public infrastructure should eventually provide automatic update information so new builds do not need to be manually distributed.

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

Use reasonable retry backoff.

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

Current development release:

`releases\v0.3-beta\`

Current executables:

- `releases\v0.3-beta\Client\PeerChat.Desktop.exe`
- `releases\v0.3-beta\Server\PeerChat.Server.Admin.exe`
- `releases\v0.3-beta\Server\PeerChat.Server.exe`

Temporary compiler output belongs under:

`build\v0.3-beta\`

Do not create confusing candidate/modern/latest/test release directories.

One clearly versioned packaged release should be the version used for real testing.

Future versions should continue this versioned release approach.

---

# DEVELOPMENT MILESTONES

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

## MILESTONE 3 — AUTOMATIC DIRECT CONNECTION + MESSAGE ORDERING

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

**Milestones 1 and 2 are complete.**

The current development target is:

# MILESTONE 3 — AUTOMATIC DIRECT CONNECTION + MESSAGE ORDERING

Focus now on:

**friend DM** → **authorized candidate lookup** → **authenticated direct connection** → **deterministically ordered local encrypted chat**

Preserve the completed account, friend, manual P2P, TLS identity, firewall, notification, and
encrypted-history behavior. Current implementation details and test state belong in
`PROJECT_STATUS.md`; protocol fields belong in `docs/DISCOVERY_PROTOCOL.md`.

Do NOT begin:

- NAT traversal
- relay implementation
- updater implementation
- disappearing messages
- group networking
- Discord bridge
- voice
- video
- screen sharing
- file transfer
- multi-device synchronization

Preserve:

- existing direct manual P2P fallback
- encrypted local history
- account/discovery functionality
- cryptographic peer identities
- frozen v0.1-beta fallback and active v0.3-beta release structure
- modern WPF UI
- Server Admin/headless-server separation

Work incrementally and test both same-LAN and public forwarded-port direct connections before
declaring Milestone 3 complete.

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
8. Continue to the next milestone unless a genuine architectural/security decision requires user input.

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
