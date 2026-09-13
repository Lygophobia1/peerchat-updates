# PeerChat development status

> Documentation authority
>
> - Product goals and roadmap: `PEERCHAT_DEVELOPMENT_GOAL.md`
> - Current implementation status: `PROJECT_STATUS.md`
> - Current system architecture: `ARCHITECTURE.md`
> - Discovery protocol contract: `docs/DISCOVERY_PROTOCOL.md`

**Known Good Baseline:** v0.1-beta  
**Current Development Version:** v4-dev.2 (`4.0.0-dev.2`)
**Current Milestone:** v4 Development updater delivery proof
**Current Priority:** physically prove the updater handoff with the first v4 Development package, then use the verified Development publishing workflow

## Current builds

- The r8 presentation baseline is now manually accepted: normal DMs/groups, New Group while a
  group is selected, anchored group/member/invite controls, header call controls and capped
  diagnostics, profile chooser selection/double-click, and dark-theme readability passed without
  changing networking, voice, group, profile, or identity behavior.
- Current uncheckpointed updater candidate:
  `releases\v0.3-beta-updater-candidate-final\Client`. It contains the desktop, native core, and
  standalone updater runtime plus a fresh runtime-only `PACKAGE-SHA256.txt` manifest.
- First publish-ready v4 Development package:
  `releases\v4-dev.1\DevelopmentFeed\PeerChat-v4-dev.1.zip`, with matching
  `releases\v4-dev.1\DevelopmentFeed\manifest.json`. The ZIP/manifest hash and byte size are
  verified locally and its root contains only the nine declared Client runtime entries. It is not
  remotely deliverable yet: its package/feed URLs remain placeholders and unsigned Development mode
  remains disabled by default.
- Current published Development update: `v4-dev.2` / `4.0.0-dev.2`. Its GitHub prerelease asset
  and the public `development/manifest.json` were verified to agree on the package URL, size, and
  SHA-256. Only the Development channel advanced; Release remains unchanged and fail-closed.
- New retained `4.0.0-dev.2` history-paging/startup candidate:
  `releases\v4-dev.2-history-paging-runtimefix\Client`. The direct desktop build had compiled
  against `PeerChat.Updater.Core.dll` without staging it beside `PeerChat.Desktop.exe`; this caused
  the physical `FileNotFoundException`. `build-desktop.ps1` now stages the required updater runtime
  trio, and the existing package verifier requires the Client runtime set and checks the desktop's
  managed assembly references before its native import/load audit. Clean package verification passed;
  retain the issue as awaiting physical startup confirmation and do not republish the Development feed.
- The first real GitHub Development attempt verified/downloaded/extracted v4 successfully but hit
  a Windows sharing lock while renaming the old Client directory to its rollback backup. The original
  desktop had exited; diagnosis showed the external launcher could inherit `Client` as its working
  directory and thereby self-lock it. The installer still retries only sharing/lock `IOException`
  values around that one rename with bounded 125/250/500/1000/2000 ms delays. The launcher now sets
  its external staging directory as its working directory and normalizes it again at updater startup.
  The existing updater suite now passes 42 assertions, including retry, external-working-directory,
  valid/invalid Release signature, metadata tamper, unknown/revoked key, and rotation-overlap cases.
  Rebuilt retry-fix payload: `releases\v4-dev.1-retry-fix\DevelopmentFeed\PeerChat-v4-dev.1.zip`
  with its matching local `manifest.json`; it is not published automatically.
- Physical upgrade host baseline: `releases\v0.3-beta-updater-retry-host\Client`. Its desktop
  remains `0.3.0-beta`, while its updater host/core contain the retry fix and its Development feed
  configuration points at the real GitHub raw manifest with the explicit Development-only unsigned
  opt-in. The original known-good baseline, prior failed transaction evidence, and v4 packages
  remain retained separately.
- Replacement physical upgrade host: `releases\v0.3-beta-updater-retry-host-selflockfix\Client`.
  It keeps the `0.3.0-beta` desktop and retry-capable updater while also carrying the external-launcher
  working-directory fix. It is staged for the next physical Development update attempt only; neither
  v4 package nor its GitHub manifest was rebuilt, modified, or published.
- Fresh physical upgrade host: `releases\v0.3-beta-updater-retry-host-selflockfix-fresh\Client`.
  It is a newly staged `0.3.0-beta` host built from the source containing both external-launcher
  working-directory protections, points at the published Development feed, and passed the 35-assertion
  updater suite directly against its packaged updater core. It exists only for the next physical
  upgrade test and does not modify the published v4-dev.2 asset or either feed manifest.
- `scripts\publish-update.ps1` now provides one explicit Development/Release publishing surface.
  Development dry runs build, package, audit, ZIP, hash, and generate a local candidate manifest;
  publication additionally requires an explicit clean `peerchat-updates` checkout and authenticated
  `gh`, uploads/verifies the GitHub asset before moving the channel manifest, and never guesses or
  clones the feed repository. Release exact-byte promotion now signs the canonical manifest with the
  operator's Current User certificate-store key and verifies it through the same embedded public-key
  verifier used by packaged clients before any remote action. Release remains fail-closed for unsigned,
  unknown-key, revoked-key, malformed, invalid, or package-mismatched input.
  It retains the physically accepted two-way direct voice, separate-network STUN/ICE, meters, and
  3+ client group behavior. It prevents discovery refresh from tearing down a healthy verified route,
  gives each local profile an exclusive process lease while allowing different profiles to run in
  separate processes, automatically persists a free port when distinct profiles share a saved port,
  and keeps outgoing group invitees visible as `Pending Invite`. The r8 presentation pass keeps the
  header call strip with internally scrolling collapsed diagnostics, makes the right sidebar a stable
  global Security & Status surface, moves DM/group inspection and management into anchored header
  flyouts, keeps New Group permanently available in the Groups tab, and completes the flat dark
  profile chooser styling without changing profile, call, group, connection, or identity paths.
- The r5 physical test passed two-way voice across two separate home networks with server-reflexive
  ICE candidates. A live LAN-to-phone-hotspot transition was not performed because the hotspot was
  unavailable; do not treat that specific transition as manually accepted.

- Client: `releases\v0.3-beta\Client\PeerChat.Desktop.exe`
- Server Admin: `releases\v0.3-beta\Server\PeerChat.Server.Admin.exe`
- Headless Server: `releases\v0.3-beta\Server\PeerChat.Server.exe`
- Temporary output: `build\v0.3-beta`
- Packager: `scripts\package-v0.3-beta.ps1`

`releases\v0.1-beta` remains the immutable fallback. Its hashes are recorded in
`releases\v0.1-beta\RELEASE-MANIFEST.txt`.

## What currently works

- Each stable local profile now has one durable server-account binding and DPAPI-protected optional
  session credential. Opening the profile automatically validates/restores its own account session;
  server failure leaves the local core, history, trust, groups, and Direct P2P available offline.
- New unlinked profiles offer Sign In, Create Account, or Continue Offline after their isolated local
  identity opens. Profile/account bubbles show the linked account and online/offline state. A linked
  profile cannot silently authenticate as another account, and an unreadable binding fails closed.
- Server logout revokes only that profile's presented session. Local sign-out preserves the account
  binding, identity, trust, groups, and history. Device presence cannot reassign one Peer ID to a
  different account.
- Milestone 1 account creation/login, protected sessions, public device registration, presence,
  case-insensitive search/suggestions, and restart-persistent discovery data.
- Milestone 2 friend request, accept/reject/cancel, persistent friendship, remove, block/unblock,
  offline recovery, and authenticated event notifications.
- Manual server-independent P2P, mutual Schannel TLS, explicit peer verification, reconnect,
  delivery acknowledgements, offline outbox, and locally encrypted SQLite history.
- Automatic friend DM connection using authorized, fresh LAN/public candidates and an expected
  cryptographic device identity. Actual subnet masks decide whether a private candidate is local.
- Friend DMs now use stable account conversation IDs. A protected per-profile account-to-device
  index merges offline history and later live traffic into one conversation, updates display names
  in place, and retains explicit verification when the account presents a different device Peer ID.
- The Connection card exposes event-driven Try Reconnect, compact state/last-failure text, a safe
  troubleshooting block, and a bounded per-profile read-only connection log. Reconnect reuses the
  selected conversation's current identity/candidates and never creates another contact or trust path.
- Saved-contact reconnect now uses jittered exponential backoff from five seconds to a five-minute
  cap. Presence/candidate changes and Try Reconnect can trigger one immediate attempt. Native events
  carry attempt/expected-peer/candidate/source/route metadata into per-peer WPF diagnostics, so two
  failing friends cannot overwrite each other's details. Rejected wrong-identity endpoints pause
  automatic retry without being trusted or rebound.
- Deterministic logical message ordering in protocol/storage with stable duplicate handling and
  legacy-history compatibility.
- One installation-wide, Private-network-only inbound TCP firewall rule scoped to the exact packaged
  client and listener port; matching rules are silently reused across profiles without elevation.
- Debugging Tools back up the protected profile, reset by moving the live profile into a timestamped
  backup, and restore with an automatic safety backup. Server data is never modified.
- Startup selects a stable-ID local profile before constructing/starting the native core. Profile
  cards support add, rename, recoverable remove, last-used state, and opening the selected profile;
  the main profile bubble reopens the chooser through a clean restart.
- A per-profile exclusive process lease is acquired before native/SQLite startup. Different profiles
  may run concurrently; launching the same profile twice exits cleanly with a clear message. A saved
  port collision between different profiles automatically assigns and persists a free port, while an
  explicit port override remains strict.
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
  demote, remove, and leave actions. Outgoing invitees remain visible as `Pending Invite` in the
  group preview/details until signed group state resolves them.
- Native one-to-one calls use the verified mutual-TLS friend connection and a negotiated
  voice capability. One active call per profile supports outgoing/incoming ringing, answer, decline,
  hang-up, busy, timeout, deterministic simultaneous-call resolution, disconnect, and shutdown cleanup.
  The authenticated channel carries WebRTC SDP/ICE signaling for libdatachannel 0.24.3. Direct ICE,
  ECDSA DTLS-SRTP, RTP jitter handling, and Opus 1.5.2 media run in native C/C++; WPF exposes Call,
  Answer/Decline, state/duration, refreshable microphone/output selection, Mute, Deafen, Hang Up,
  and bounded Copy Voice Diagnostics. Default capture resolves to a concrete WinMM ID before opening;
  explicit microphones are preserved. Opus RTP timestamps advance correctly, and audio-device failures remain visibly attached
  to the call instead of being overwritten by Connected. The reused negotiated `sendrecv` track owns
  both RTP submission and receive callbacks, fixing the physical-test symptom where both peers
  submitted Opus frames but neither received a remote track callback. Media
  devices and networking remain closed while Idle. Audio is never recorded or persisted.
- The Voice Call card uses dark, keyboard-accessible microphone/output selectors and two read-only
  meters. Native generic media telemetry measures existing captured PCM before Opus and decoded PCM
  entering playback, with time-based decay, availability states, and lifecycle reset. WPF receives
  only two normalized values plus flags; it never sees or stores PCM. The display maps RMS over a
  -60 dBFS to 0 dBFS range so low decoded speech levels remain visible without altering audio gain.
- The packaged native core now statically links the remaining TDM-GCC stack-protector runtime.
  Release packaging rejects non-Windows DLL imports and performs a restricted-search load smoke, so
  the client does not depend on a compiler installation or development-machine `PATH`.

## Stabilization changes

- Repeated calls: microphone errors no longer mark a verified text peer offline; explicit native
  track deletion removes C API registry leaks. Device startup is serialized by the session owner.
  Media counters/deafen reset on reuse and a failed initial offer is cleaned up even before Outgoing.
- Audio: buffer generation prevents requeue after callback-driven mute/reopen. Exact open, prepare,
  queue and start results plus bounded buffer/position diagnostics separate device failure stages.
  Capture-open/start failure preserves receive-only playback. Bluetooth driver behavior is unproven.
- Removed the diagnostic media interceptor: the pinned library's implementation consumes incoming
  packets without forwarding its result vector. Track counters remain; pre-track counts are labelled
  unavailable. The pinned WebRTC/Opus versions and RTP clock are unchanged.
- Connection repair refreshes authoritative friend state and candidates, retires stale manual
  attempts, and preserves healthy sessions. Wrong-identity suspension begins immediately; a routine
  unchanged presence update does not retire an active handshake. Copy Details is peer-scoped.
- Authoritative endpoint refresh no longer requests route invalidation while a live session exists;
  the native core independently retains a matching verified winner and diagnoses that decision.
  Actual local network-generation changes still perform ordered whole-peer invalidation.
- Invite eligibility now comes from stable account/friend model state, excluding members/invitees
  only, with live picker refresh. A separate dialog snapshot previously diverged from the model.
  Offline/no-endpoint friends appear; invitation signing still requires verified device trust.
  Sammy's exact historical cache contents were unavailable, so that individual cause is not proven.

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

The focused WPF settings smoke passes against a newly built client: all Settings categories can be
selected, Updates exposes its own check control, Advanced restores the existing direct-P2P controls,
and the sidebar version is read from packaged version metadata. The broader packaged desktop UI smoke
was not used for this narrow pass because its pre-existing isolated encrypted-data fixture fails before
contacts are loaded.

The r6 stabilization checks pass: the native reconnect regression proves both stale and exact
authoritative endpoint refreshes leave a verified winner connected and usable; the managed adapter
passes pending-invite projection and full group convergence; the packaged bootstrap regression runs
two different profiles that initially share one saved port, verifies both reach native Ready after
automatic port reassignment, and rejects a second launch of the same profile with exit code 2 instead
of hanging. A → B → A restart switching and installation firewall reuse remain green. The
package dependency audit confirms `PeerChat.Core.dll` imports Windows system DLLs only.

Current reliability candidate: full native regression passes, including three consecutive media
sessions with bidirectional injected Opus; mocked WinMM open/start failures, three device sessions,
callback mute and handle cleanup; reconnect repair and wrong-identity suspension. Managed adapter
tests pass three connected calls alternating caller, unchanged online state after audio error, and
four-friend invite eligibility with an offline/no-endpoint/unverified friend. Both candidate WPF
clients pass messaging, devices, clipboard and connection-log UI checks; candidate friend UI passes
account restore/isolation, direct messaging, logout and offline startup. Package import/load audit
passes. These automated results do not prove physical Bluetooth capture or audible playback.

The meter candidate passes focused normalized RMS/decay/reset tests, mocked capture/playback
telemetry and three-device-session lifecycle coverage, plus a packaged voice-panel smoke for dark
keyboard/accessibility styling and Refresh Devices selection persistence. A broader managed test
currently encounters `Could not encrypt local data`; the untouched earlier test binary fails at the
same point, so this is an environmental DPAPI test-host problem rather than a meter regression.
Real local WebRTC/Opus loopback also verifies that non-silent decoded PCM produces nonzero output
telemetry through three successive media sessions. The r2 display maps the observed quiet decoded
level onto a readable dB range and includes both normalized levels in Copy Voice Diagnostics.

The network candidate compiles the full packaged WPF/native client and passes its clean-package
native import/load audit. Focused subnet assertions prove `192.168.1.14/24` and
`192.168.1.17/24` classify as the same LAN in both directions, remain so after call teardown, and
record ignored-interface reasons without aborting the scan. The native reconnect regression passes,
including authoritative same-endpoint source/route replacement while trust remains enforced. The
broader adapter run reaches the pre-existing hardware voice portion before failing with no connected
call; the candidate-selection assertions execute earlier and pass.

The r2 network candidate adds event-driven recovery for live interface/IP changes. It debounces
Windows address notifications, invalidates old sessions and persisted private routes outside the
new subnet, republishes the current presence candidate, refreshes authoritative friend state, and
reruns normal candidate selection. Discovery-channel reconnection repeats publish-before-refresh.
Targeted coverage verifies the house-LAN-to-`10.211.241.x` transition rejects the former
`192.168.1.x` route, native invalidation stops its scheduled retries, and identity/trust remain
separate from candidate state.

Physical r2 logs proved the hotspot-to-public TCP/TLS/Peer-ID route succeeds, then receives EOF
about 60 ms later. Code tracing identified peer-wide old-route invalidation as the lifecycle race:
an authoritative update on the remote client could close the newly authenticated public connection
while intending to retire only the former LAN endpoint. The r3 fix scopes authoritative invalidation
to exact host/port, reserves all-route invalidation for the local network-generation transition, and
allows a new authenticated route to supersede an older idle duplicate. Disconnect diagnostics now
identify remote EOF/TLS close, local invalidation, duplicate arbitration, socket failure, timeout, or
application-protocol rejection. Offline discovery presence remains advisory.

Focused r3 reconnect tests establish an authenticated replacement route, deliver a delayed stale
endpoint invalidation, and confirm the replacement remains usable. Reconnect backoff, candidate
correlation, and wrong-identity suspension remain green. The broader native node regression passes
six peers, 15 mutual-TLS links, disconnect/restart, offline outbox, and encrypted histories.

Physical r3 testing proved text and calls remained usable while one UI projected a TCP timeout.
The root cause was attempt-level events overwriting account-conversation state: WPF treated every
outbound attempt/error/retry as authoritative and the per-peer diagnostic tracker replaced its
candidate/source/transport fields even when an inbound authenticated winner was live. The r4
projection stores the winner's connection ID, endpoint, source, route, and direction separately;
loser failures remain visible only under latest failed attempt. Only disconnect of the winning ID
clears Connected. Native incoming sessions now receive IDs and direction/source metadata.

Focused managed coverage passes inbound winner plus outbound timeout, outbound winner plus inbound
duplicate loser, delayed old-generation disconnect, final winner disconnect, and per-peer diagnostic
correlation. Native reconnect and six-peer/15-link session regressions remain green; reconnect
backoff remains dormant whenever another usable winner exists.

- `scripts\run-tests.ps1`: passed six encrypted profiles, DPAPI storage/tamper rejection,
  mutual TLS, 15 direct links, Unicode messages, ACKs, reconnect, offline outbox, restart,
  deduplication, malformed transport isolation, a signed group-ledger test with eight identities,
  and a four-client direct group test covering roles, unauthorized changes, fanout, offline delivery,
  convergence, restart persistence, real two-peer direct WebRTC/DTLS-SRTP negotiation, unanswered
  timeout, disappearance while ringing, stopped-profile restart/reconnect, active-call cleanup,
  device-free Opus encode/decode, bidirectional injected PCM through real local ICE/DTLS-SRTP into
  both remote decode/playback queues, native audio queue/mixer and stale-device fallback bounds, two simultaneous failing peers,
  capped exponential reconnect jitter, immediate manual/candidate retry, endpoint metadata, and
  wrong-identity retry suspension.
- `tests\test-discovery.ps1`: passed authentication, session-scoped logout, device/account ownership,
  presence/search, friend lifecycle, authoritative snapshots, and database restart persistence.
- `tests\test-desktop.ps1`: passed protected profile-account binding/session persistence, A/B
  mismatch rejection, profile-scoped sign-out, unreadable-binding detection, managed/native adapter,
  direct-message workflow, stable
  account-conversation persistence/isolation, old/new device history merge, name reconciliation,
  explicit changed-device verification, isolated per-account attempt diagnostics, wrong-identity
  no-rebind behavior, group creation, membership convergence, group text, secure
  voice negotiation, device selection/refresh, privacy-safe current/recent call diagnostics,
  mute/deafen, and hang-up through the managed/native bridge.
- `tests\test-desktop-ui.ps1`: passed two r8 candidate-packaged WPF clients, manual P2P, composer,
  history/reconnect, compact connection status, Copy Details, the read-only bounded log, bidirectional
  delivery, Groups/DM draft isolation, persistent New Group, anchored group header flyouts, and the
  permanent global Security & Status sidebar.
- `tests\test-voice-ui.ps1`: passed the header call-strip lifecycle, inline diagnostics hidden by
  default and capped at 300 px with internal scrolling, light-on-dark control text, dark device
  controls, meters, and refresh selection persistence.
- `tests\test-friend-ui.ps1`: passed two packaged clients finding/accepting, authenticated direct DM,
  manual authoritative refresh/reconnect preserving the same conversation and ready state,
  protected-session restart restore, correct A → B → A account/device selection, A-only sign-out and
  server revocation, retained local trust/history, and server-unavailable Offline / Direct P2P startup.
- Profile-manager and native-adapter coverage passes idempotent legacy migration, preserved Peer ID,
  trust and readable encrypted history, distinct A/B identities, no A state in B, A → B → A
  restoration, and B-only reset/restore.
- `tests\test-profile-bootstrap.ps1`: passed the progressive flat chooser, existing/new selection,
  clean exact A → B → A process replacement, concurrent distinct profiles, clean same-profile
  rejection, and installation-level Private firewall-rule reuse.
- `scripts\verify-client-native-dependencies.ps1`: passed the final package import audit and native
  load with DLL resolution restricted to the package directory and System32. Final core imports are
  `advapi32`, `bcrypt`, `crypt32`, `iphlpapi`, `kernel32`, `msvcrt`, `ncrypt`, `secur32`, `winmm`,
  and `ws2_32`; no WebRTC/Opus/MbedTLS/libSRTP/MinGW runtime DLL remains dynamic.
- The packaged friend test now continues direct messaging after both discovery event channels stop.
- Manual packaged acceptance passed local-network and separate-network/hotspot direct P2P using a
  fresh account/device, fresh-machine firewall approval, and successfully authenticated chat.
- Physical LAN firewall testing also passed one Private-only rule reused without repeat UAC.
- The standalone updater core is now compiled and packaged beside the client. Settings → Updates
  persists an installation-wide Release/Development channel (never in a profile), checks only on
  explicit user action, and downloads/verifies/stages before launching a copied updater outside the
  target installation. The client exits cleanly, the updater replaces sibling directories, and the
  replacement confirms health only after local native profile/core startup; otherwise it rolls back.
  Targeted updater coverage passes 42 assertions for semantic version policy, manifest validation,
  HTTPS/hash checks, resumable download, downgrade rejection, transaction recovery, confirmed
  replacement, failed-first-launch rollback, Release signature/tamper/key-rotation policy, and the
  Release default channel. Clean packaging also
  passes restricted native loading/import audit and creates a fresh runtime-only package with hashes.

## Current blocker / next action

The current Development feed now points at v4-dev.2. The next updater-specific action is physical
startup and in-place update proof from the retained `v4-dev.2-history-paging-runtimefix` candidate,
including successful first-launch health confirmation and profile/history preservation. The 4.0.0-dev.2
Development ZIP published at 07:05 has a different hash from this later candidate, so it cannot be
represented as the candidate's exact-byte promotion artifact. Release signing is now configured
fail-closed with trusted key ID `peerchat-release-2026-01`; its private key exists only in the release
operator's Current User Windows certificate store and the updater embeds only public verification
material. The raw Release feed is configured as
`https://raw.githubusercontent.com/Lygophobia1/peerchat-updates/main/release/manifest.json`, but that
manifest intentionally does not exist until a real signed Release is published. Release remains
blocked by artifact acceptance/version readiness: the source/client is still the Development
pre-release `4.0.0-dev.2`, and no accepted exact-byte stable Release candidate has been selected.

The former isolated encrypted-data failure was diagnosed as a test-host context mismatch, not a
production storage defect. The Codex sandbox runs under `CodexSandboxOffline` while inheriting
`C:\Users\Livel` profile paths; its current-user DPAPI master-key lookup returns Win32 error 2
(`ERROR_FILE_NOT_FOUND`). The same existing storage, node, and group-node tests pass under the
normal interactive Windows user token. DPAPI errors now retain the Win32 result safely. Automated
encrypted-storage tests must run under the intended interactive user context; current-user scope,
entropy, ciphertext format, and existing encrypted-data compatibility remain unchanged.

## Known bug backlog

Keep this list current. When work enters a listed subsystem, first assess whether the bug has a
small, evidenced fix within that task. Report the root cause, smallest fix, affected files/subsystems,
and targeted validation before changing it. Do not widen unrelated work to fix a backlog item; leave
it open with the reason when it needs architectural or higher-risk work. A bug is removed only after
code/test evidence or relevant physical validation—not by assumption.

- **Open — intermittent desktop CLR/WPF crash (`0xe0434352`).** Intermittent packaged desktop failure;
  repository logs contain neither a matching stack trace nor a reliable reproduction. Scope when
  investigating: WPF startup, dispatcher/background exception boundaries, and packaged startup
  diagnostics.
- **Open — false `Network route changed` / `Network connection lost` teardown.** Targeted route-winner
  protections and regressions exist, but retain this until physical validation establishes that healthy
  sessions are no longer ended by endpoint/discovery refreshes.
- **Resolved (test-host context; normal-user regression passed) — isolated encrypted-data regression.**
  The first `CryptProtectData` call failed only under the sandbox token with `ERROR_FILE_NOT_FOUND`
  because that token did not own the inherited Livel DPAPI profile path. The normal-user storage,
  node, and group-node tests pass. Storage now reports `DPAPI Win32 error <code>` on protect/unprotect
  failure. No scope, entropy, plaintext fallback, ciphertext format, or profile data changed.
- **Implementation pending runtime/physical validation — conversation history paging.** The desktop
  now requests a newest-first 100-message initial page, anchors it at the bottom, tracks loaded and
  exhausted peer/group pages, and requests the next older page only near the top while preserving the
  visual viewport after prepend. DM/group presentation still deduplicates by message ID and applies
  deterministic ordering. Group metadata refresh no longer emits its full signed-message ledger;
  a dedicated native page request reads the validated ledger without changing replication or storage.
  The expanded existing group-node regression compiles, but its execution is currently blocked before
  group setup by the separate known DPAPI test-host failure (`Could not encrypt local data`); retain
  this backlog item until that targeted runtime regression and a large-history manual check pass.
- **Resolved (targeted WPF smoke) — sidebar/version display mismatch after update.** The sidebar
  literally rendered `0.3.0-beta`; it now projects `PeerChat.VersionInfo.Display`, the same packaged
  version source as Settings/About. A later physical in-place update remains useful confirmation of
  the complete updater handoff, but no updater behavior changed for this display fix.
- **Resolved (targeted WPF smoke) — Updates content appeared under Advanced.** The update controls
  were appended to the Advanced panel. They now reside in their own selected Updates page; updater
  transport, validation, and staging were not changed.
- **Resolved (targeted WPF smoke) — Settings navigation only showed Advanced.** The left labels were
  non-interactive text blocks. They are now selectable navigation controls with lightweight current
  or future-category content, while Advanced retains the existing direct-P2P/profile tools.

## Known/deferred work

- TURN and optional encrypted relay remain future transport work. Neither may become group authority.
- Group voice, video, screen sharing, file transfer, disappearing messages, Discord
  integration, and multi-device synchronization remain deferred.
- Build emits existing non-fatal C# warnings for intentionally fire-and-forget WPF actions.
- Latest project checkpoint: `81abba3 Fix voice audio transfer and diagnostics`; current fixes uncheckpointed.
