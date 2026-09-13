# PeerChat development status

> Documentation authority
>
> - Product goals and approved planned direction: `PEERCHAT_DEVELOPMENT_GOAL.md`
> - Current implementation, evidence and backlog: `PROJECT_STATUS.md`
> - Implemented architecture only: `ARCHITECTURE.md`
> - Discovery wire/API and authority contract: `docs/DISCOVERY_PROTOCOL.md`

**Accepted product baseline:** v4.0.0, physically accepted by the user.
**Actual source/runtime metadata:** `4.0.0-dev.2`; not a formally published stable v4.0.0.
**Current stage:** Stage 3 direct-session continuity ownership complete after targeted gates.
**Next action:** await separate authorization for Stage 4 delivery-above-sockets work; do not begin it implicitly.
**Release work:** stable metadata/artifact selection, signing-key backup and formal publication remain
separate; no publication, final tag, key change or package rebuild in this documentation pass.

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
- Stage 3 implementation checkpoint on `refactor/session-transport`: recorded by the implementation
  commit immediately preceding the status-record commit.
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

These are the current boundaries after Stage 3.

- WPF still observes Windows network/friend changes and selects discovery candidates, but it sends
  immutable recovery intents and the current network generation to the native boundary. The native
  `SessionTransportManager` is the single owner for direct attempt identity, active-winner selection,
  retry/backoff, route cancellation, stale-token rejection and authoritative session state.
- Whole-peer invalidation is generation-checked. A delayed old-network invalidation cannot cancel a
  newly authenticated route, exact-endpoint refresh preserves the active verified winner, and an old
  transport close cannot clear a newer winner.
- Duplicate arbitration remains immediate and deterministic. The manager records the winning
  transport instance and rejects or retires losers; retry work remains dormant while a usable winner
  exists. Physical sockets still run on the existing Node worker.
- Storage outbox is durable, but DM/group inflight ACK eligibility still belongs to individual
  connections. Coordinated overlap and draining ACK ownership are not implemented.
- TCP control/text and WebRTC media are separate live paths; current call teardown still depends
  on control disconnect/invalidation. Logical call/control/media lifetime separation is planned.
- Native/managed events now carry complete profile/session epoch and monotonically revisioned session
  snapshots. WPF applies only newer snapshots for online state and winning-connection projection;
  failed/superseded legacy attempt events remain diagnostics and cannot downgrade a live winner.
- Candidate selection and physical connection execution remain existing code. There is no alternate
  route overlap, make-before-break handoff, transport drain or negotiated handoff protocol yet.

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

## Approved direction and stage gates (not implemented)

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
| 6 | Voice control migration; call/control/media separation, stale epochs, control-gap hang-up and repeated-call/profile tests, plus physical voice. |
| 7 | Pinned-library media transition proof before implementation; injected media/lifecycle plus physical network-change and audible continuity tests. |
| 8 | Significant-checkpoint full regression, package audit and physical messaging/voice/network-transition acceptance. |
| 9 | Retire proven-obsolete paths with affected gates; client modularization only afterward under separate authorization. |
| 10 | Separately authorized screen sharing, then video/files; no implicit TURN/relay/onion implementation. |

During migration, require exactly one production reconnect/winner decision owner per moved responsibility. Planned
runtime epoch, instance ID, route revision and handoff ID must have explicit lifetimes; callbacks
validate their applicable tokens. Candidate failure and discovery loss cannot close healthy winners.
Durable delivery is above transports; control/messages and media may have separate active bindings.

Before Stage 5, review exact wire capability/envelopes, simultaneous-proposal arbitration and bounded
drain/fallback policy. Before Stages 6–7, review control-loss/hang-up semantics and pinned-library
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
- **Open pending physical validation — false `Network route changed` / `Network connection lost`
  teardown.** Stage 3 now centralizes generation-checked invalidation and proves with automated tests
  that delayed old-generation work cannot close a newer verified winner. A live LAN→hotspot→LAN
  transition has not been physically retested, so this bug is not silently closed.
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

Stage 4 delivery migration, client modularization, screen sharing, video, files, group voice, TURN,
encrypted relay, onion, disappearing messages, external bridge and multi-device synchronization remain
deferred. Preserve the accepted stack and begin the next stage only with separate authorization.
