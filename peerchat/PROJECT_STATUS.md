# PeerChat development status

> Documentation authority
>
> - Product goals and approved planned direction: `PEERCHAT_DEVELOPMENT_GOAL.md`
> - Current implementation, evidence and backlog: `PROJECT_STATUS.md`
> - Implemented architecture only: `ARCHITECTURE.md`
> - Discovery wire/API and authority contract: `docs/DISCOVERY_PROTOCOL.md`

**Accepted product baseline:** v4.0.0, physically accepted by the user.
**Actual source/runtime metadata:** `4.0.1-dev.1`; Development prerelease published. This is not a stable Release.
**Current stage:** Stage 8 one-to-one screen sharing passed current physical two-profile acceptance in the isolated `stage8-single-gpu-worker-20260923` build: 2560×1440 sharing, an active voice call, text, Stop Share, and a second Start Share all remained usable. The accepted fix gives one encoder worker ownership of PeerChat D3D11 copy/conversion work and leaves the WGC callback as a bounded newest-frame handoff. Cross-network acceptance remains unproven. Oversized-message crash, simultaneous-share overload and VirtualBox capability remain separate investigations.
**Next action:** verify an installed Development client receives `4.0.1-dev.1`, then perform sustained cross-network screen sharing and a longer multi-machine stress run as follow-up acceptance.
**Latest Git checkpoint:** `f728784` (`Publish Development screen sharing candidate`); Development feed commit `2a91e18` publishes `v4.0.1-dev.1`.
**Release work:** stable metadata/artifact selection, signing-key backup and formal publication remain
separate; no release publication, final tag, or key change occurred in this stage.

**Development publication evidence:** `v4.0.1-dev.1` is a GitHub prerelease at
`https://github.com/Lygophobia1/peerchat-updates/releases/tag/v4.0.1-dev.1`. Its
runtime-only ZIP is `3,147,619` bytes with SHA-256
`1298baaa009e5bad267e9395b1a42dcfb8616b28612b66ba94d1c0865ad97ca6`; the pushed
`development/manifest.json` references those exact values. Packaging required and audited
`PeerChat.ScreenMedia.dll` alongside `PeerChat.Desktop.exe` and the updater runtime.

## Stage 8 implementation and remaining acceptance

- Receiver diagnostics now distinguish complete access units accepted by the bounded decoder
  queue from units refused because its eight slots are full, and expose queue depth plus the
  current decode/presentation stage and its elapsed time. In the physical snapshot, 43 accepted
  and 382 refused units do not contradict each other; one decoded and zero presented frames
  point to a downstream stall, but the precise blocking call needs the new physical snapshot.
  Local authenticated two-profile fixed 720p, 1080p and 1440p window tests passed; a 90-second
  2560×1440 real-monitor run at 30 FPS presented 2,381 frames and recovered after 19 bounded
  queue drops. That 30-FPS local run does not reproduce or clear the physical 60-FPS failure.
  `build/stage8-single-gpu-worker-20260923` passed physical two-profile acceptance: full-resolution
  sharing, voice and messaging coexistence, Stop Share, and a subsequent share all worked. The
  shared publisher now stages `PeerChat.ScreenMedia.dll` into its runtime-only Client ZIP and
  audits its Windows imports so a Development package cannot silently omit the adapter.
- Two actual isolated WPF clients from that test package connected over verified peer TLS and
  direct ICE at requested 2560×1440/60 FPS and 25 Mbps. The receiver presented 253 frames;
  later diagnostics showed zero decode-queue depth and a worker waiting for input. A subsequent
  active-motion run reproduced the freeze after 1,571 encoded frames: sender capture callback
  and encoder worker both remained in broad D3D11 stages for more than 22 seconds; receiver had
  accepted 1,501 access units, decoded/presented 1,463, then waited on an empty queue with no
  discontinuity. Stop Share stayed in `closing capture and encoder` for over 10 seconds and
  blocked native-worker message processing. These facts locate the failure on the sender but
  do not yet distinguish a GPU mutex wait from a stuck D3D11 call. The finer-trace physical run
  did: after 5,340 encoder inputs and 5,263 receiver presentations, `VideoProcessorBlt` remained
  in progress for 35 seconds while the capture callback waited on the GPU mutex; receiver decode
  queue stayed empty. This identifies the immediate blocking call, though not whether the driver
  or unprotected shared-context access ultimately causes it. The adapter now enables D3D11
  multithread protection before passing the shared device to the asynchronous encoder, following
  the Windows Media Foundation device-sharing guidance. Targeted compile and packaged dependency
  checks passed. The interactive Windows GPU probe also passed on the actual 2560×1440 monitor:
  hardware H.264 encode, adapter decode/presentation, three continuous monitor frames, and a
  captured window frame. Physical retest still stalled in `VideoProcessorBlt` after 1,814
  encoded frames; Stop remained in encoder close after 20 seconds. The new candidate confines
  PeerChat's immediate-context copy and conversion to one encoder worker and makes the WGC
  callback a one-frame, drop-stale handoff. Its targeted compile, short interactive GPU probe,
  and package dependency checks passed. The subsequent physical two-profile run exercised the
  working full-resolution share with voice, messaging, Stop Share and a replacement share. That
  clears the observed local stop/hang path; sustained cross-network stress remains unproven.
- The screen-media stop path previously allowed late remote-track registration and an in-flight
  RTP callback to overlap decoder destruction. Stop now rejects callback work for the retired
  generation, synchronizes remote-track registration, and drains RTP callback access before
  closing media. Targeted local transport and two-profile tests passed, including repeated
  start/stop, a hidden capture source, post-stop messaging, and a 1920×1080/60 FPS run. This
  addresses a concrete lifetime race. Together with the one-worker capture/conversion boundary,
  the current local physical test completed Stop Share and a subsequent share cleanly. A hard
  deadline against a future blocked Windows GPU/codec call is not claimed.
- Authenticated two-profile integration passed: verified peer TLS carried share offer/answer/candidates,
  separate ICE/DTLS-SRTP carried H.264 to GPU presentation, text remained usable, a media-generation
  restart after a simulated network change resumed rendering, and stop/decline returned both peers
  to idle. The local WPF smoke, package dependency/import verification, and isolated candidate build
  passed. These do not prove real two-PC or cross-network behavior.
- The current publish candidate is built from the accepted `stage8-single-gpu-worker-20260923`
  source; `PeerChat.ScreenMedia.dll` is now required in every runtime-only Client package and
  receives its own Windows import audit. Current local two-profile acceptance covered sender/
  receiver UI, a full-resolution monitor, active voice plus share, messaging, Stop Share and a
  repeated start. Recovery across a real network transition remains follow-up work.
- Once-per-second telemetry reports observed capture/encode/send-API/receive/render rates and bounded
  queue drops. Selected ICE route and effective encoded/decoded dimensions are authoritative. The
  pinned libdatachannel C API does not expose media RTT, wire throughput, jitter or reliable loss
  percentage, so those fields are explicitly unavailable rather than inferred.

- `tests/test-screen-pipeline.ps1 -Run` passed in the interactive Windows user session with a
  2560×1440 monitor, D3D11-backed Windows.Graphics.Capture, GPU BGRA→NV12 conversion, AMD hardware
  H.264 Media Foundation encoding, Microsoft H.264 decoding to a D3D11 NV12 texture, GPU
  NV12→BGRA conversion, and DXGI swap-chain presentation.
- The captured/decoded raw frames remained on GPU surfaces with no raw-pixel CPU readback. The
  H.264 compressed bytes crossed GPU/CPU boundaries for the prospective libdatachannel API.
- This local hardware smoke test was followed by authenticated client integration and current
  local two-profile physical acceptance. Production MinGW build/dependencies remain unchanged.
- `windows_media/peerchat_screen_media.h` now defines the narrow Windows DLL ABI, and its first
  implementation enumerates current monitors/windows and reports Media Foundation hardware
  encode/decode availability. The test enumerated two monitors and nine windows and reported
  H.264/HEVC hardware encode plus H.264/HEVC decode; AV1 was unavailable. The DLL is now
  packaged in the isolated Development candidate and invoked by the desktop client. Its MSVC
  runtime is statically linked; import audit
  showed Windows-inbox dependencies only. The adapter owns no session/call/trust/route state.
- The adapter now has event-driven continuous monitor/window capture and a bounded in-memory
  Annex-B H.264 encoder queue, plus a bounded decoder queue with D3D11 presentation. The same
  local hardware test passed three continuous monitor frames, a specific-window frame, and
  adapter decoder rendering. `tests/screen_media_transport_test.cpp` passed a one-PC two-endpoint
  ICE/DTLS-SRTP test: 20 H.264 access units submitted, 345 RTP packets received, 20 units
  reassembled, and two frames decoded/presented. That earlier test verified media mechanics;
  the later authenticated two-profile test above covers local trust/control/recovery, but remote
  hardware compatibility beyond the accepted local test machine still requires physical acceptance.

## Frozen checkpoint and branches

- Common accepted-source checkpoint: `ea6102b74ea4da11e44863ead33c64ff51fa5223`
  (`Freeze accepted PeerChat v4.0.0 product baseline`).
- `release/v4.0.0`: frozen product baseline for formal Release work, packaging and only necessary
  critical release-blocking fixes.
- `refactor/session-transport`: current branch; both branches were created from that exact commit.
  This documentation synchronization is subsequent work, not a change to the frozen Release branch.
- No final `v4.0.0` tag; reserve it for formal signed Release publication.
- Architecture-plan checkpoint on `refactor/session-transport`:
  `a76a4aded84108dfadc5a2817ce5a9dbb1bec5d8` (`Document session transport migration plan`).
- Stage 2 implementation checkpoint on `refactor/session-transport`:
  `f7259595da9e4d19847f2f71c038b6bfc6980800` (`Introduce Stage 2 direct transport wrapper`).
- Stage 3 implementation checkpoint on `refactor/session-transport`:
  `38e6a4ea4f5778bfe31def98e95e5980703b8e79` (`Centralize Stage 3 session continuity ownership`).
- Stage 3 documentation checkpoint and Stage 4 starting point on `refactor/session-transport`:
  `6b5934dc196b56431a9480167f5c4fc1eea1e2b3` (`Record Stage 3 implementation checkpoint`).
- Stage 4 implementation checkpoint on `refactor/session-transport`:
  `372663a` (`Extract Stage 4 durable delivery ownership`).
- Stage 4 documentation checkpoint and Stage 5 starting point:
  `dfe1926e6af7be56de64de88152fd91a98f6fb7d`; worktree was clean before Stage 5.
- Stage 5 implementation checkpoint: `eeb15fc` (`Implement Stage 5 capability-gated direct
  transport handoff`). Automated gates passed; physical two-PC handoff acceptance remains pending.
- Stage 5 documentation checkpoint and Stage 5.5 starting point: `f58ef81`. Stage 5.5 retains the
  completed Stage 3–5 ownership model and does not migrate voice/media.
- Stage 5.5 accepted checkpoint: `f5368fd0738a5556d0924fe3b3bb3ad0a4ff2a05`. Physical direct →
  relay → direct recovery, idle relay, failed direct probe, ACK/message continuity, friend-add relay,
  established-friend relay, trust and network-switch acceptance passed before Stage 6 began.
- The checkpoint retained existing client/native, tests, updater, packaging and docs. Generated
  packages/builds/fixtures, credentials/private keys and profile/history data were excluded.
  Historical packages, updater evidence and the existing master branch were preserved.

## Accepted and retained artifacts

- Accepted retained client: `releases\v4-dev.2-history-paging-runtimefix\Client\PeerChat.Desktop.exe`.
  The user's later product acceptance supersedes older pending history-paging/startup notes.
  Acceptance does not imply exact-byte equivalence to an earlier published Development ZIP.
- Last recorded published Development: `v4-dev.2` / `4.0.0-dev.2`. The earlier published ZIP has
  different bytes from the later history-paging candidate. Its remote state was not rechecked or
  changed during the architecture audit/synchronization.
- Historical packaged layout: `releases\v0.3-beta\Client` and `releases\v0.3-beta\Server`.
  Server executables are `PeerChat.Server.Admin.exe` and `PeerChat.Server.exe`.
- Immutable fallback: `releases\v0.1-beta`, with `RELEASE-MANIFEST.txt`.
- Retained updater investigation outputs include `v0.3-beta-updater-candidate-final`,
  `v0.3-beta-updater-retry-host`, `v0.3-beta-updater-retry-host-selflockfix`,
  `v0.3-beta-updater-retry-host-selflockfix-fresh`, `v4-dev.1` and `v4-dev.1-retry-fix`
  under releases. These are historical evidence, not competing current baselines.
- Build/package entry points: `scripts/build-desktop.ps1`, `scripts/package-v0.3-beta.ps1`,
  `scripts/verify-client-native-dependencies.ps1`. Build output stays under `build`.
  The audit used `build/audit-baseline`, without replacing accepted release artifacts.
- Do not manually clean historical source, packages or failed transactions. Fresh runtime-only
  staging and verification remain the packaging approach; never silently rebuild for exact-byte promotion.

## Implemented baseline

- Profile-first startup with a stable local ID, isolated device key/Peer ID, encrypted history/trust,
  contacts, outboxes, groups and account binding. DPAPI-protected account sessions auto-restore when
  available; discovery failure still permits local/offline and manual Direct P2P use.
- One profile/core per process, exclusive same-profile lease, concurrent distinct profiles, clean
  same-profile rejection, restart-based switching and free-port assignment on saved-port collision.
  Explicit port overrides remain strict. Migration/backup/reset/restore retain isolation and ciphertext.
- Stable social DM conversations reconcile current/historical verified device histories in place.
  Device changes still require explicit trust; account association never silently grants device trust.
- Account/discovery/friend persistence and authorized candidate exchange. Direct mutual-TLS P2P,
  logical message ordering, delivery ACKs, encrypted offline outbox and duplicate suppression.
- DM/group history presentation pages 100 newest messages, starts at the newest end, loads older pages
  near the top and preserves viewport position. Group paging reads the validated ledger without
  changing its signed-event authority, replication or message IDs.
- Client-owned signed group ledgers with arbitrary membership, roles, invites and per-peer fanout.
  Three clients are a minimum test, not a cap. Pending outgoing invitees remain visible.
- Native one-to-one libdatachannel 0.24.3 / Opus 1.5.2 voice: verified control signaling, direct
  ICE/STUN, DTLS-SRTP, fresh PeerConnection per call, repeated-call cleanup, selected WinMM devices,
  mute/deafen, bounded diagnostics and privacy-safe level telemetry. No audio recording/persistence.
- Accepted flat dark WPF presentation: header actions and slim call strip, capped/collapsed inline
  diagnostics, anchored DM/group flyouts, persistent New Group, compact global Security & Status,
  readable profile chooser, working Settings navigation, dedicated Updates and runtime version display.
- Installation-wide Private-only firewall-rule reuse; per-peer bounded connection diagnostics and
  manual reconnect through the existing trusted connection path.
- Separate updater with resumable download, HTTPS/size/SHA validation, safe extraction, journal,
  sibling replacement, first-launch health confirmation and rollback. Required updater runtime
  assemblies accompany the desktop; native packaging requires Windows-inbox DLL imports only.

## Current recovery limits relevant to the refactor

These are the current boundaries after Stage 5.5.

- WPF observes Windows network/friend changes and supplies current same-subnet LAN, public and relay
  availability observations with the network generation. It no longer selects LAN versus public or
  launches a failure-triggered public fallback. The native `SessionTransportManager` is the single
  owner for direct/relay attempt identity, fallback timing, active-winner selection, retry/backoff,
  route cancellation, stale-token rejection, promotion/drain and authoritative session state.
- Whole-peer invalidation is generation-checked. A delayed old-network invalidation cannot cancel a
  newly authenticated route, exact-endpoint refresh preserves the active verified winner, and an old
  transport close cannot clear a newer winner.
- Ordinary startup/legacy duplicate arbitration remains immediate and deterministic. Explicit
  alternate routes for mutually capable peers use authenticated readiness, coordinated promotion
  and bounded draining. The manager remains the sole winner/retry owner on the existing Node worker.
- The profile-scoped `DeliveryManager` owns DM/group pending retrieval, authenticated transport
  submission eligibility, destination-scoped ACK authorization, resend after transport retirement,
  and incoming DM deduplication. Individual connections no longer own inflight message/event sets.
- A draining transport can acknowledge only IDs it submitted to its authenticated destination and
  cannot accept new DM/group submissions. Stage 5 now exercises that ownership during real overlap;
  immediate candidate failure can restore the retained, still-trusted route.
- TCP/relay control and WebRTC media are separate live paths. Call signaling may use either verified
  reliable winner; audio remains on independent ICE/DTLS-SRTP. Temporary control loss and media
  interruption are projected independently; Stage 5.5 alone restores the reliable route while the
  Stage 7 call owner waits within its bounded control/media recovery deadline.
- Native/managed events now carry complete profile/session epoch and monotonically revisioned session
  snapshots. WPF applies only newer snapshots for online state and winning-connection projection;
  failed/superseded legacy attempt events remain diagnostics and cannot downgrade a live winner.
- Direct remains preferred. After an 8-second bounded direct window the manager may request one
  authenticated server relay route; failed direct probes continue on manager-owned backoff while a
  healthy relay remains the winner. A relay can never replace a healthy direct winner. Direct
  recovery uses the Stage 5 authenticated handoff before relay drain. Voice media remains direct ICE;
  authenticated call control is transport-independent across the reliable direct/relay winner.

## Stage 5.5 implemented boundary

- `SessionTransportManager` owns the ordered LAN/public candidate plan, bounded relay fallback,
  per-route backoff, stale generation rejection, direct preference, relay winner lifetime and
  direct recovery. Candidate providers do not choose or promote a route.
- `RelayTransport` is a managed physical adapter only. It opens the existing certificate-pinned
  authenticated server TLS connection, attaches to the manager-issued attempt, and carries bounded
  framed opaque bytes. Relay reads have no discovery-request timeout, so a healthy idle winner stays
  open until a peer/server/shutdown close; writes retain their bounded timeout. It owns no retry,
  fallback, winner, trust or identity policy.
- The native core runs the unchanged Schannel peer TLS protocol inside the relay byte stream.
  Expected Peer ID, certificate/key verification, explicit changed-device trust, durable delivery,
  destination-scoped ACK authorization, resend and deduplication are identical to direct transport.
  Server authorization never grants peer trust.
- The server authorizes relay pairing only when both authenticated accounts/devices attach
  reciprocally and are established friends or have a current pending friend request in either
  direction. Pairing IDs are random, short-lived, bound to the exact account/device pair, and each
  side may attach once. Third-party, stale, duplicate and cross-profile attachment fails closed.
  Per-frame size is capped at 64 KiB; the native outgoing relay queue is capped at 1 MiB, while
  synchronous server forwarding relies on TLS/socket backpressure rather than an unbounded queue.
- The outer account/server TLS stream and framed relay session are distinct from the inner peer TLS
  stream. Discovery JSON cannot be interpreted as peer bytes. The server does not decrypt, persist,
  inspect or log message content, peer TLS material or relay payloads.
- Existing manual/server-independent Direct P2P remains available. A discovery/relay outage cannot
  disturb a healthy direct winner. Stage 5.5 remains frozen; Stage 6 only permits authenticated call
  control on its reliable relay winner and never routes media/audio through the server relay.

## Stage 6 implemented boundary

- A profile-scoped native `CallManager` is authoritative for the typed `CallId`, monotonically
  increasing lifetime, direction, state, participants, timeout and mute/deafen state. Stage 6 still
  permits one active one-to-one voice call, but its state is minimally participant-shaped rather than
  embedding one remote peer into the ownership model. No group call, mesh or SFU behavior exists.
- WPF sends commands and renders explicit call state/type/lifetime events; it no longer infers call
  lifecycle from display strings. Native media callbacks carry call ID, participant and lifetime, so
  completion from an older call cannot mutate a replacement call. `VoiceMedia` remains a direct
  ICE/DTLS-SRTP media engine that reports facts; it does not own call state or retry policy.
- Verified call-control frames use the current `SessionTransportManager` reliable winner, whether
  direct or opaque relay. Relay availability is not treated as media capability. The inner peer TLS,
  expected Peer ID and existing trust checks remain authoritative; audio never enters the relay.
- Per-profile DPAPI-protected voice preferences persist only selected input/output device identity
  and real native input/output software gain (0–200%). Missing saved devices fall back to current
  Windows system defaults. Ephemeral call state and audio are not persisted.
- Settings → Voice exposes working device refresh/selection and native software levels. Existing
  mute/deafen and bounded input/output telemetry remain call controls. WinMM/Opus does not provide
  supported AEC, noise suppression, AGC, sensitivity or push-to-talk, so no nonfunctional toggles are
  exposed. TURN/media relay and group calls remain deferred.

## Stage 7 implemented boundary

- The logical `CallSession` now owns a separate media state, monotonically increasing generation,
  recovery capability, bounded deadline/retry pacing, control-availability grace and authoritative
  selected-media route. A stable CallId and call lifetime survive successful media replacement.
- Every recovery offer, answer and candidate is capability-gated and generation-tagged. Media events
  are accepted only for the current call lifetime, participant and generation; late old-generation
  candidates, answers, connected and failed completions are ignored.
- Meaningful network-generation changes and ICE disconnect/failure interrupt only `VoiceMedia`.
  Recovery recreates a fresh libdatachannel PeerConnection and exchanges negotiation over whichever
  authenticated direct/opaque-relay control winner Stage 5.5 currently provides. Audio is never sent
  through the PeerChat server relay.
- Recovery is bounded to 30 seconds in production with paced attempts. Missing control suppresses
  negotiation without starting another reconnect owner. Healthy media may continue during temporary
  control loss; unavailable control plus unavailable media eventually ends cleanly. Local hangup
  always tears down local media immediately.
- Mute, deafen, selected devices and input/output software gain survive media replacement. Older
  clients without the recovery capability keep compatible Stage 6 initiation and terminate cleanly
  rather than receiving new recovery frames.
- Security & Status renders authoritative reliable and media methods separately: Direct LAN/Internet
  or PeerChat Server Relay with peer TLS, versus LAN/NAT-traversed ICE (and TURN only if genuinely
  selected) with DTLS-SRTP. Media recovery state/generation is native-owned; WPF does not infer it.

## Stage 2 implemented boundary

- `session_transport_types.hpp` defines portable profile/device session identity, runtime epoch,
  transport instance, independent route revision, channel requirements, state/result vocabulary,
  and immutable payload-free transport events.
- `direct_transport_adapter.hpp` observes one existing direct connection and exposes identity,
  capability, route, send-acceptance, receive-size and close-completion semantics. It cannot open,
  send on, retry, arbitrate, cancel, promote, drain, or close a socket.
- `peer_session.hpp` holds the initial verified-device logical session and per-channel observed
  bindings. `session_transport_manager.hpp` is profile-scoped and rejects stale-epoch observations.
- Node attaches the adapter to inbound/outbound legacy connections and reports lifecycle decisions
  after the existing implementation makes them. Failures in this optional projection cannot change
  legacy networking behavior.
- The current Node/WPF code remains authoritative for retries, candidate selection, invalidation,
  duplicate winners, trust, ACK/outbox, group delivery and voice. Stage 2 adds no scheduler, thread,
  socket, protocol field/type/capability, bridge export, discovery operation, or WPF orchestration.

## Stage 3 implemented boundary

- `SessionTransportManager` now owns the existing direct path's per-profile/session transport records,
  unique attempt/instance tokens, active binding, deterministic duplicate arbitration, bounded
  exponential retry with injected jitter, cancellation eligibility and network-revision validation.
- Node still owns its single worker, resolvers, Winsock/Schannel objects and physical close/connect
  execution. It registers lifecycle observations with the manager and executes explicit manager
  decisions instead of maintaining a second reconnect/winner authority.
- Candidate failure, stale close, rejected duplicate, delayed retry and stale network invalidation do
  not replace or downgrade an authenticated active winner. Identity mismatch still stops that attempt
  and suspends automatic retry without changing trust or rebinding the account/device identity.
- The bridge emits self-contained revisioned session snapshots. Managed conversation and diagnostics
  projections accept only newer revisions; legacy lifecycle events are compatibility diagnostics once
  an authoritative snapshot has arrived.
- No peer wire frame, discovery API, trust rule, storage format, ACK/outbox ownership, group ledger,
  media transport or voice state machine changed. Those later responsibilities were not pulled into
  Stage 3.

## Stage 4 implemented boundary

- `delivery_manager.hpp` is the profile-scoped delivery owner above sockets. Existing encrypted
  `Storage` remains the durable source of truth for DM/group outboxes and delivered state.
- A transport is authorized only after mutual TLS, expected Peer ID, local trust and Stage 3 winner
  activation. Submission and ACK eligibility are keyed by delivery kind, exact destination, ID and
  transport instance; another peer or an unsent ID cannot clear durable delivery state.
- Transport retirement removes runtime eligibility without deleting pending storage. Restart and
  replacement transports therefore resend through the existing outbox; receiver storage and the
  signed group ledger retain their existing duplicate suppression and ordering rules.
- No storage schema/ciphertext, peer frame, discovery API, trust behavior, group-event contract,
  bridge/UI contract, media behavior or voice state changed. No Stage 5 handoff capability, message,
  route overlap or make-before-break policy was introduced.

## Stage 5 implemented boundary

- `DirectHandoff` is a small state machine composed by `SessionTransportManager`, not another
  socket, winner or retry owner. Hello capability 16 gates type-14 coordination; connection intent
  bit 32 distinguishes deliberate candidates from simultaneous startup duplicates. Legacy peers
  retain existing frames/arbitration and receive no new coordination packets.
- Mutual TLS, verified expected identity, trust and exchanged per-connection nonce pairs bind
  candidate readiness to the current profile epoch, local instance and retained route. The lower
  verified Peer ID coordinates simultaneous proposals. Old-generation candidate callbacks cannot
  promote over the live winner; revoking trust also prevents fallback.
- Prepare/Ready/Commit/Promoted/Confirm messages coordinate promotion, with 500 ms bounded retries,
  a 10-second preparation/confirmation limit and 3-second drain. Candidates cannot downgrade a
  healthy winner on failure; completed/expired attempts cannot extend their deadline through replay.
  Persistent coordination loss may fall back to ordinary reconnect; atomic agreement under permanent
  partition is not claimed. Exact wire/phase details are in `ARCHITECTURE.md`.
- Stage 4 delivery eligibility supports overlap, authorized ACKs on old transports, resend/dedup,
  and restoration of the retained route. Close-completion diagnostics retain local/remote origins
  even when the logical session stays connected.
- No voice/media routing, storage format, discovery API, account/trust model, WPF, updater or release
  behavior changed. Physical direct-text network handoff remains an explicit acceptance gate.

## Approved direction and remaining stage gates

The approved direction is in PEERCHAT_DEVELOPMENT_GOAL.md. The Desktop
`PeerChat_Abstraction_Architecture_Prompt.md` is a task plan subordinate to the four authoritative
documents, not another architecture authority. Focused classes/files separate distinct state,
lifetime, concurrency and testing responsibilities; composition replaces a giant orchestrator.
Interfaces/factories need concrete value.

| Stage | Next responsibility/gate |
|---|---|
| 1 | Baseline, audit and plan synchronization; code/doc consistency and limited baseline verification. |
| 2 | Types and direct wrapper, unchanged behavior/wire; native/bridge build and targeted adapter/reconnect/node tests. |
| 3 | Single winner/retry/cancellation owner, tokens and snapshots; deterministic old-callback/loser/presence cases plus affected friend/profile integration. |
| 4 | Delivery above sockets; destination ACK, drain, resend/dedup, storage/restart and group-node tests. |
| 5 | Capability-gated direct handoff; two-peer arbitration/loss/replay/fallback and legacy compatibility, then physical text handoff. |
| 6 | One-to-one voice ownership and profile voice settings; implementation complete at `c9943b5`, physical acceptance pending. |
| 7 | Implementation/automated gates complete: bounded replaceable media generations, stale-generation rejection and voice recovery; physical network-change/audio continuity pending. |
| 8 | One-to-one screen sharing passed current local two-profile physical acceptance; Development publication and later cross-network/stress acceptance remain. |
| 9 | Webcam video. |
| 10 | Group calls without replacing the participant-oriented call owner. |
| 11 | File transfer and richer messaging features. |
| 12 | Product usability, reliability, packaging and UX polish. |
| 13 | Node retirement, source-tree organization, proven legacy-path removal and remaining architectural cleanup. |

During migration, require exactly one production reconnect/winner decision owner per moved responsibility. Planned
runtime epoch, instance ID, route revision and handoff ID must have explicit lifetimes; callbacks
validate their applicable tokens. Candidate failure and discovery loss cannot close healthy winners.
Durable delivery is above transports; control/messages and media may have separate active bindings.

Stage 5 wire capability/envelopes, simultaneous-proposal arbitration and bounded drain/fallback are
recorded in ARCHITECTURE.md and exercised by the existing reconnect suite. Before Stages 6–7, review control-loss/hang-up semantics and pinned-library
ICE-restart versus PeerConnection-replacement evidence. These are later design gates, not permission
to implement protocol/media behavior in Stage 2.

## Evidence and testing status

### Architecture audit — limited verification at baseline freeze

- Fresh isolated desktop/native build: passed.
- Existing updater suite: **42 assertions passed**, without a signing-key operation.
- Required managed-runtime set and native import/restricted package/System32 load audit: passed.
- Git whitespace check: passed.
- Existing non-fatal C# warnings remained: duplicate VersionInfo definitions, unused locals and
  unawaited WPF calls. They were not changed.
- No full regression or physical acceptance was rerun by the audit. This document-only follow-up
  synchronizes the plan/docs and checks scope/consistency; it makes no new runtime-validation claim.

### Stage 2 targeted verification

- Native/core and managed desktop bridge build: passed with the pre-existing VersionInfo, unused-local
  and unawaited-call warnings unchanged.
- Extended reconnect/direct-wrapper test: passed under the normal Windows user context. It covers
  untrusted identity exclusion, legacy activation projection, distinct reliable/realtime requirements,
  stale epoch rejection, old-close/new-binding isolation, payload acceptance vocabulary, the unchanged
  fixed Accepted frame, reconnect backoff, route invalidation, manual/presence retry, diagnostics and
  wrong-identity suspension.
- Six-node direct test: passed with 15 mutual-TLS links, 30 messages/ACKs, disconnect, restart/reconnect,
  durable outbox, contacts and encrypted histories.
- Managed profile/adapter tests: passed, including profile isolation/migration/reset/restore, direct
  mutual TLS and trust, changed-device verification, bidirectional text/ACK, groups and repeated voice.
- A sandboxed first reconnect run reproduced the known DPAPI test-host context limitation; the same
  executable passed under the intended interactive Windows user token. Encryption was not bypassed.
- No UI, updater, packaging, full regression, or new physical test was run because Stage 2 did not
  change those boundaries. `protocol.hpp`, the C ABI/managed bridge contract, and discovery wire/API
  remain unchanged.

### Stage 3 targeted verification

- Strict-warning native reconnect build and contract-only test: passed. Coverage includes the single
  continuity owner, deterministic winner/retry/cancellation tokens, revisioned snapshots, direct
  wrapper separation and unchanged peer-protocol framing.
- Live reconnect/diagnostics test under the intended Windows user token: passed. Coverage includes
  two-peer reconnect, bounded backoff, immediate presence/manual retry, route invalidation,
  wrong-identity suspension, stale timer/callback rejection and active-winner preservation.
- Managed desktop/profile/adapter suite under the intended Windows user token: passed, including
  authoritative snapshot revision gating and delayed legacy loser/disconnect suppression. The first
  sandbox-token run reproduced the documented DPAPI test-host limitation (Win32 2); encryption was
  not bypassed and the normal-user run passed.
- During implementation, the affected Node, group-node, six-process direct-network and voice call-state
  suites also passed. They covered 15 mutual-TLS links, messaging/ACK/outbox/restart, four-member group
  delivery and the unchanged verified voice-call lifecycle. No package or physical network-transition
  acceptance was performed for Stage 3.

### Stage 4 targeted verification

- Strict-warning native builds passed for storage, group-ledger, node, group-node and reconnect tests.
- Storage delivery tests passed under the intended Windows user token. They cover exact-destination
  ACK authorization, cross-peer and unsent-ID rejection, draining-transport ACK, no new sends while
  draining, restart persistence, replacement-transport resend and incoming duplicate suppression.
- Group-ledger delivery tests passed under the intended Windows user token. They prove per-recipient
  group outbox isolation and that one member cannot acknowledge another member's pending event.
- Six-node direct messaging passed with 15 mutual-TLS links, 30 messages/ACKs, disconnect,
  restart/reconnect, durable outbox and encrypted history. Four-member group-node delivery and the
  unchanged Stage 3 reconnect/continuity regression also passed.
- A fresh desktop/native bridge build passed. Existing VersionInfo, unused-local and unawaited-call
  warnings were unchanged. No UI/package/updater or physical test was run because Stage 4 changes no
  wire, bridge, media, packaging or presentation boundary.

### Stage 5 targeted verification

- Strict-warning native builds passed. Existing reconnect contracts cover authenticated readiness,
  loss/replay of each promotion phase, total coordination loss, stale profile/instance/network tokens,
  bounded drain, immediate promoted-route failure/fallback, trust revocation and legacy gating.
- Real local TLS handoff passed with simultaneous startup and alternate-route attempts, 24 messages
  each direction during overlap plus one after drain, authorized ACKs, no duplicate visible messages,
  and no disconnected session snapshot. Both old sockets must close; one may observe remote EOF
  before its own timer. The test now checks close completion rather than requiring two timer events.
- Existing backoff/route/diagnostic/identity tests passed. The three-real-connect-failures collection
  deadline increased from 5 to 15 seconds after repeat timing failures; exponential minimums, cap,
  manual/presence retry and wrong-identity checks remain unchanged. Production retry timing is unchanged.
- Storage passed overlapping/draining ACK, cross-destination/unsent rejection, fallback send eligibility,
  restart/resend, dedup, ciphertext scan and tamper checks. Six-node messaging/outbox/restart,
  four-member group-node and unchanged voice call-state regressions passed during implementation.
- Retained pre-handoff harness interoperability passed in both initiation directions with explicit
  trust, Unicode text/ACK and changed endpoint hints; no unexpected protocol messages.
- Final native/desktop build and managed adapter suite passed: encrypted profile isolation,
  migration/reset/restore, changed-device approval and existing group/call integration. Existing
  C# VersionInfo/unused-local/unawaited-call warnings remain. DPAPI tests used the normal Windows user.
- Final runtime audit passed: required managed updater files, Windows-only native imports and
  package/System32-only restricted load. Candidate: `build/stage5/Client/PeerChat.Desktop.exe`,
  still `4.0.0-dev.2`; copy the entire Client directory, not only the EXE.
  Core SHA-256: `c3630523babc913995f86459df3c7432dc3d0551c18b2589351b90b0fe8ce41f`.
  Desktop SHA-256: `3c36c5243e5022c68684212604e4c36ac7a863b3cbaec63e10c7c2e5d1f094bd`.
- Physical two-PC direct-text continuity and LAN→hotspot→LAN overlap/fallback remain untested here.
  Run both candidates, exchange text while changing the route without restarting, confirm stable
  conversation/trust and no missing/duplicate messages, then repeat in reverse. Local loopback is
  not physical network-change acceptance. Voice continuity migration and Stage 6 have not begun.

### Stage 5.5 targeted verification

- Strict-warning native reconnect/session build and full reconnect integration passed under the
  normal Windows user. New coverage proves the bounded direct window, LAN-before-public ordering,
  relay fallback, relay-active direct probing/backoff, failed-candidate winner preservation, stale
  network rejection, direct-winner priority and delayed-relay loser rejection.
- Two native nodes passed the unchanged inner peer TLS and explicit trust exchange over an in-memory
  opaque relay adapter, then exchanged durable messages and authorized ACKs in both directions.
- Discovery/server integration passed for pending friend-add relay bootstrap, established-friend
  relay, reciprocal account/device pairing, exact opaque-byte forwarding and unrelated-account
  rejection. A physical VM/NAT test exposed that the relay had inherited the discovery request's
  10-second receive timeout; the log showed the authenticated relay closing on that exact read timeout.
  The targeted integration now keeps an established relay idle beyond that former cutoff and then
  proves unchanged byte forwarding. Durable account/friend restart coverage remained green.
- Reconnect/session coverage now registers a direct-recovery probe beside an active relay, closes the
  probe with a local authentication timeout, and proves the relay remains the connected winner, the
  loser remains separately correlated, and no duplicate relay attempt starts.
- Fresh desktop/native build and managed profile/adapter regressions passed. The sandbox-token DPAPI
  limitation reproduced once; the intended normal Windows user run passed without weakening DPAPI.
- Physical Stage 5.5 acceptance passed at `f5368fd`: direct unavailable → opaque relay messaging with
  unchanged trust → authenticated make-before-break direct recovery, idle relay stability, failed
  direct recovery without relay loss, friend-add/established-friend relay and network switching.

### Stage 6 targeted verification

- The full native regression gate passed after the ownership migration: encrypted storage, delivery,
  groups, direct handoff/reconnect, Stage 5.5 relay, TLS, six-node/process messaging, WebRTC media,
  Opus and audio lifecycle. The relay fixture additionally exchanges an authenticated call invite and
  decline over the opaque reliable winner while durable messaging remains connected.
- Voice lifecycle coverage passes outgoing/incoming, answer, decline, caller cancel before answer,
  timeout, either-side end, repeated calls, deterministic simultaneous calls, stale lifetime rejection,
  negotiation/active disconnect and profile shutdown. Injected PCM still crosses direct ICE/DTLS-SRTP.
- Native audio coverage verifies saturating 0–200% software gain without opening physical devices.
  Managed adapter coverage verifies explicit call state/type/lifetime projection, old-call event
  rejection, mute/deafen, device selection, volume bounds, DPAPI preference restart, missing-device
  fallback and no persisted call state.
- The packaged hidden-WPF smoke passes Settings navigation, the working Voice device/volume controls,
  active call strip, capped diagnostics, dark device selectors, meters and refresh selection retention.
  The packaged runtime dependency/import audit passes with updater assemblies present and only
  Windows-inbox imports in `PeerChat.Core.dll`.
- Physical two-client acceptance remains required for real microphone/output selection, persisted
  levels, direct call audio, call control while reliable messaging uses relay, and repeated teardown.
  Stage 6 is not marked physically accepted.

### Stage 7 verification

- Focused native voice coverage passes stable-CallId recovery, repeated media replacement,
  old-generation rejection, preserved mute/deafen, bounded deadline ownership, local and remote
  hangup during recovery, simultaneous-call arbitration and normal Stage 6 lifecycle behavior.
- The existing reconnect/relay suite now answers a voice call over an authenticated opaque relay,
  establishes independent ICE media, changes network generation, and proves replacement-media
  negotiation crosses relay control without disturbing its reliable winner.
- The complete native/checkpoint gate passes: encrypted storage, groups, durable delivery,
  reconnect/handoff/relay, TLS, six-node/process messaging, WebRTC/Opus/audio and 42 updater
  assertions. DPAPI-backed suites were run under the normal interactive Windows user context.
- Desktop adapter/profile tests and the two-instance packaged WPF smoke pass. The voice UI smoke
  verifies stale-generation rejection, accepted current-generation close, capped diagnostics, and
  separate authoritative PeerChat-relay/Direct-LAN versus NAT-traversed-media labels.
- `build\stage7-media-continuity\Client` passes managed-runtime completeness, restricted native
  load and import audit; `PeerChat.Core.dll` imports Windows-inbox DLLs only.
- Physical acceptance remains pending: two real clients must verify two-way audio, repeated network
  switches, same logical CallId, recovered media generation, preserved devices/gain/mute/deafen,
  control recovery through direct or server relay, and clean hangup/profile shutdown.

### Historical regression and physical evidence

These results predate the architecture audit; they are not fresh execution of the migration gates.

- Native regression previously covered encrypted storage/tamper rejection, mutual TLS, six peers/
  15 links, restart/outbox/ACK/dedup, malformed-message isolation, reconnect/backoff, wrong-identity
  suspension, signed group authorization/convergence and four-client group delivery.
- Existing voice suites exercised real local ICE/DTLS-SRTP with injected bidirectional Opus through
  three media sessions, call timeouts/disconnect/profile cleanup, mocked WinMM failures and repeated
  device lifecycle. They do not substitute for hardware/NAT acceptance.
- Managed adapter, discovery/friend and profile/bootstrap suites covered account binding/isolation,
  protected restore, explicit changed-device verification, simultaneous loser/winner projection,
  DM/group behavior, A→B→A restart switching and concurrent distinct-profile startup.
- Packaged WPF tests covered Settings/Updates/version, chooser, header calls/diagnostics, group flyouts,
  history/reconnect and safe clipboard/log actions. Package audits covered managed updater runtime
  and Windows-inbox native imports.
- User physical evidence includes LAN and separate-network direct text, fresh identities/firewall
  approval, two-way voice, separate-home-network STUN/ICE, functioning meters, 3+ group behavior,
  accepted presentation and the later accepted history-paging/runtimefix product baseline.
- Earlier hotspot recovery logs exposed authenticated-then-EOF and winner-projection defects.
  Later separate-network success does not prove a live LAN→phone-hotspot→LAN transition; that
  acceptance was unavailable when the hotspot could not be used. Keep it an explicit refactor gate.
- The earlier isolated DPAPI failure was later resolved as the test-host context issue below.
  Earlier fixture failures must not be reported as a still-unexplained production encryption defect.

Fast/local tests target the changed subsystem. Integration tests cover affected boundaries. Full
regression is reserved for significant checkpoints/releases or major cross-boundary changes. Extend
existing suites/shared fixtures instead of adding one-off files or rebuilding redundant packages.
Run encrypted-data fixtures under the intended Windows user context; never bypass DPAPI to pass them.

## Release/updater boundary — unchanged by this work

Development publishing uses the shared publisher with an explicit clean feed checkout and authenticated
git/gh. Asset upload/verification precedes manifest movement. The earlier published Development ZIP
and the accepted later candidate are not interchangeable exact-byte artifacts.

Release verification is configured fail-closed with trusted key ID `peerchat-release-2026-01`;
the private key remains in the operator's Current User Windows certificate store and only public
verification material is embedded. No key export/backup, signing, feed access or publication occurred
in this architecture pass. Formal stable metadata/artifact selection, backup/restore verification
and publication remain separate Release work. The configured raw Release URL is
`https://raw.githubusercontent.com/Lygophobia1/peerchat-updates/main/release/manifest.json`;
the last recorded state has no published Release manifest. No remote state was revalidated here.

Historical rename-lock failures motivated the bounded sharing-lock retry and external-launcher
working-directory fix. The current updater stages/runs outside Client, retains rollback evidence
and validates health after native startup. Preserve all failure evidence and original artifacts.
Do not modify updater architecture, signing policy, Defender, encryption or Release-branch behavior
to facilitate the networking abstraction.

## Known bug backlog

Do not silently close bugs. A subsystem task may fix an evidenced, narrow issue only within its
authorized scope: explain cause/files/fix, run targeted tests, and report resolved/partial/open.
Broader or uncertain changes remain open with the missing evidence.

- **Open — intermittent CLR/WPF `0xe0434352`.** No matching actionable stack/reliable reproduction
  in the reviewed evidence. Needs packaged exception/Windows crash evidence; general baseline
  acceptance is not proof this intermittent condition is resolved.
- **Resolved by Stage 5.5 physical acceptance — false `Network route changed` / `Network connection
  lost` teardown.** Generation-checked invalidation and stale-completion rejection passed automated
  coverage, then the accepted `f5368fd` direct/relay/network-switch run confirmed the healthy winner
  remains usable across the tested transition. Reopen only if new physical evidence reproduces it.
- **Resolved — isolated encrypted-data regression (test-host context).** First CryptProtectData
  failed with Win32 2 / ERROR_FILE_NOT_FOUND under CodexSandboxOffline inheriting Livel profile paths.
  Existing storage, node and group-node tests passed under the normal interactive user token.
  Safe DPAPI Win32 diagnostics were added; current-user scope, entropy, ciphertext format and
  existing encrypted-data compatibility stayed unchanged. This does not authorize plaintext fallback.
- **Accepted in later user product baseline — DM/group history paging and updater-runtime startup.**
  Preserve newest-page/bottom anchoring, older-page viewport/dedup and group ledger semantics.
  The later user acceptance supersedes the earlier pending-startup/paging notes; no new physical
  run was performed by this audit. The required managed-runtime package check remains mandatory.
- **Resolved by targeted WPF smoke; included in accepted presentation — Settings navigation,
  Updates under Advanced, and sidebar version mismatch.** Navigation is selectable, Updates has its
  own page, and sidebar/About use runtime VersionInfo. No updater transport/validation changed.

## Deferred scope

Stage 8 physical acceptance, client modularization, video, files, group voice, configured TURN/media relay,
onion, disappearing messages, external bridge and multi-device synchronization remain deferred.
Preserve the accepted stack and begin the next stage only with separate authorization.
