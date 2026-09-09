# PeerChat development status

> Documentation authority
>
> - Product goals and roadmap: `PEERCHAT_DEVELOPMENT_GOAL.md`
> - Current implementation status: `PROJECT_STATUS.md`
> - Current system architecture: `ARCHITECTURE.md`
> - Discovery protocol contract: `docs/DISCOVERY_PROTOCOL.md`

**Known Good Baseline:** v0.1-beta  
**Current Development Version:** v0.3-beta  
**Current Milestone:** Milestone 3 — Automatic Direct Connection + Message Ordering  
**Current Priority:** remaining identity-change/restart acceptance and visible legacy-profile upgrade validation

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
  deduplication, and malformed transport isolation.
- `tests\test-discovery.ps1`: passed authentication, identity, presence/search, friend lifecycle,
  authoritative snapshots, and database restart persistence.
- `tests\test-desktop.ps1`: passed managed/native adapter and direct-message workflow.
- `tests\test-desktop-ui.ps1`: passed two packaged WPF clients, manual P2P, composer,
  Send/Attachment state, history, reconnect, and bidirectional delivery.
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

Automated and local packaged coverage is green. Physical local-network and separate-network/hotspot
direct P2P, fresh account/device setup, fresh-machine firewall approval, and authenticated chat are
manually accepted. Remaining physical acceptance is to confirm one identity-change prompt after a
Fresh Client reset, restart without another prompt, then stop discovery and confirm the established
direct chat continues.

Also run one manual upgrade against a disposable copy of a real pre-profile client state: confirm
the profile-card chooser, legacy Peer ID/history, add/rename/recoverable-remove actions, restart-based
switch, and selected-profile-only backup/reset/restore. Automated DPAPI/ciphertext migration is green,
but this visible packaged upgrade acceptance has not been performed on a user's real legacy state.

Milestone 3 must not advance to NAT traversal or relay until the remaining identity-change and
discovery-independence checks pass.

The account-isolation startup gate is implemented with restart-based switching for v0.3-beta.
Discovery/events and P2P sessions stop, the native worker closes SQLite and clears identity state,
and the replacement process selects exactly one profile before constructing its core. Later
in-process switching must preserve this same boundary and may not keep two cores open.

## Known/deferred work

- NAT traversal/STUN/ICE and relay are Milestones 4/5.
- Voice, video, screen sharing, groups, file transfer, updater, disappearing messages, Discord
  integration, and multi-device synchronization remain deferred.
- Build emits existing non-fatal C# warnings for intentionally fire-and-forget WPF actions.
- Latest project checkpoint: `Record physical P2P acceptance`
  (after `Fix packaged profile bootstrap and firewall reuse`).
