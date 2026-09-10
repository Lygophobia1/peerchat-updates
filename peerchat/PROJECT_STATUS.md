# PeerChat development status

> Documentation authority
>
> - Product goals and roadmap: `PEERCHAT_DEVELOPMENT_GOAL.md`
> - Current implementation status: `PROJECT_STATUS.md`
> - Current system architecture: `ARCHITECTURE.md`
> - Discovery protocol contract: `docs/DISCOVERY_PROTOCOL.md`

**Known Good Baseline:** v0.1-beta  
**Current Development Version:** v0.3-beta  
**Current Milestone:** Group Text Stage 1 — client-owned peer-replicated groups
**Current Priority:** packaged 3+ client group acceptance, then 1-to-1 voice foundation

## Current builds

- Client: `releases\v0.3-beta\Client\PeerChat.Desktop.exe`
- Server Admin: `releases\v0.3-beta\Server\PeerChat.Server.Admin.exe`
- Headless Server: `releases\v0.3-beta\Server\PeerChat.Server.exe`
- Temporary output: `build\v0.3-beta`
- Packager: `scripts\package-v0.3-beta.ps1`

`releases\v0.1-beta` remains the immutable fallback. Its hashes are recorded in
`releases\v0.1-beta\RELEASE-MANIFEST.txt`.

## What currently works

- Milestone 1 account creation/login, DPAPI-protected sessions, public device registration,
  presence, case-insensitive search/suggestions, and restart-persistent discovery data.
- Milestone 2 friend request, accept/reject/cancel, persistent friendship, remove, block/unblock,
  offline recovery, and authenticated event notifications.
- Manual server-independent P2P, mutual Schannel TLS, explicit peer verification, reconnect,
  delivery acknowledgements, offline outbox, and locally encrypted SQLite history.
- Automatic friend DM connection using authorized, fresh LAN/public candidates and an expected
  cryptographic device identity. Actual subnet masks decide whether a private candidate is local.
- Deterministic logical message ordering in protocol/storage with stable duplicate handling and
  legacy-history compatibility.
- One installation-wide, Private-network-only inbound TCP firewall rule scoped to the exact packaged
  client and listener port; matching rules are silently reused across profiles without elevation.
- Debugging Tools back up the protected profile, reset by moving the live profile into a timestamped
  backup, and restore with an automatic safety backup. Server data is never modified.
- Startup selects a stable-ID local profile before constructing/starting the native core. Profile
  cards support add, rename, recoverable remove, last-used state, and opening the selected profile;
  the main profile bubble reopens the chooser through a clean restart.
- Each profile isolates device identity/Peer ID, trust, encrypted history, contacts/endpoints/outbox,
  discovery configuration/session, account/friend runtime state, unread state, and pending prompts.
- Legacy state is copied to recovery and staging locations, SHA-256 verified, marked complete, and
  atomically cataloged. Original files remain recoverable and ciphertext is not re-encrypted.
- Backup, reset, and restore affect the selected profile. Reset gives only it a new Peer ID.
- Client-owned group text now uses random stable Group IDs and signed event ledgers for create,
  rename, invite/accept/decline, leave/remove, owner/admin/member roles, and messages. Group state and
  encrypted history persist per profile and replicate over authenticated direct P2P connections.
- The group implementation uses arbitrary member collections and general peer fanout; there is no
  three-member schema, protocol, routing, storage, authorization, or WPF limit. Three clients remain
  the minimum manual acceptance case, not a product cap.
- The WPF Groups tab lists groups/unread state, retains independent drafts, sends group text, displays
  a scrollable member list, and exposes role-appropriate rename, invite, accept/decline, promote,
  demote, remove, and leave actions.

## Stabilization changes

- Separated discovery service configuration from the optional self-hosted public P2P address.
  The discovery port is never used as a peer listener port; the P2P port comes from authenticated
  client presence.
- Rejected loopback, unspecified, private, link-local, shared, multicast, and reserved observed
  addresses as public P2P candidates.
- Presence/friend state now includes candidate source, observed classification, freshness,
  LAN/public endpoints, and current public device identity.
- Suppressed duplicate presence notifications when a heartbeat changes no authoritative state.
- Kept the event stream primary; reconciliation is three minutes while healthy and one minute
  while reconnecting, with bounded event reconnect backoff.
- Scoped trust suppression to expected identity, presented identity, and connection generation.
- Preserved native TLS/identity exception detail and limited public fallback to route failures.
- Fixed exact/prefix discovery SQL that referenced a nonexistent alias and returned false
  `not_found` results.

## Verification performed

- `scripts\run-tests.ps1`: passed six encrypted profiles, DPAPI storage/tamper rejection,
  mutual TLS, 15 direct links, Unicode messages, ACKs, reconnect, offline outbox, restart,
  deduplication, malformed transport isolation, a signed group-ledger test with eight identities,
  and a four-client direct group test covering roles, unauthorized changes, fanout, offline delivery,
  convergence, and restart persistence.
- `tests\test-discovery.ps1`: passed authentication, identity, presence/search, friend lifecycle,
  authoritative snapshots, and database restart persistence.
- `tests\test-desktop.ps1`: passed managed/native adapter, direct-message workflow, group creation,
  signed invitation/acceptance, membership convergence, and group text across the native ABI.
- `tests\test-desktop-ui.ps1`: passed two release-packaged WPF clients, manual P2P, composer,
  Send/Attachment state, history, reconnect, bidirectional delivery, and Groups/DM draft isolation.
- `tests\test-friend-ui.ps1`: passed two packaged clients finding, requesting, accepting,
  opening an authenticated direct DM, and exchanging a message.
- Profile-manager and native-adapter coverage passes idempotent legacy migration, preserved Peer ID,
  trust and readable encrypted history, distinct A/B identities, no A state in B, A → B → A
  restoration, and B-only reset/restore.
- `tests\test-profile-bootstrap.ps1`: passed the real packaged chooser for existing/new profiles,
  clean A → B → A process replacement, exact selected-profile startup, and installation firewall
  reuse with no rule update during either switch.
- The packaged friend test now continues direct messaging after both discovery event channels stop.
- Manual packaged acceptance passed local-network and separate-network/hotspot direct P2P using a
  fresh account/device, fresh-machine firewall approval, and successfully authenticated chat.
- Physical LAN firewall testing also passed one Private-only rule reused without repeat UAC.

## Current blocker / next action

The group backend, native transport, managed bridge, and release package are automated-test green.
Next, run the required manual packaged acceptance with at least three real clients: create a group,
invite/accept members, exchange concurrent text, change roles/name, remove and leave, restart/reconnect,
and then stop discovery while direct connectivity remains. Confirm all replicas converge and no DM,
profile, identity, or history state crosses profiles. Three clients prove group behavior but are not
a product limit.

The remaining pre-group physical checks are one identity-change prompt after Fresh Client reset and
one visible upgrade against a disposable copy of real pre-profile state. Automated coverage remains
green, but those two manual checks have not been recorded as complete.

The account-isolation startup gate is implemented with restart-based switching for v0.3-beta.
Discovery/events and P2P sessions stop, the native worker closes SQLite and clears identity state,
and the replacement process selects exactly one profile before constructing its core. Later
in-process switching must preserve this same boundary and may not keep two cores open.

## Known/deferred work

- NAT traversal/STUN/ICE and optional encrypted relay remain future transport work. Neither may
  become group authority.
- 1-to-1 voice is the next implementation stage after packaged group acceptance. Group voice,
  video, screen sharing, file transfer, updater, disappearing messages, Discord integration, and
  multi-device synchronization remain deferred.
- Build emits existing non-fatal C# warnings for intentionally fire-and-forget WPF actions.
- Latest project checkpoint: `Add client-owned peer-replicated group text foundation`
  (after `Record physical P2P acceptance`).
