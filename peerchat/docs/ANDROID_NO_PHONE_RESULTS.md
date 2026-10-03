# Android messaging no-phone completion report — 2026-10-03

## 1. Checkpoints

Backend branch `codex/android-messaging-port`:

| Commit | Responsibility |
|---|---|
| `3d6cbfc` | Windows Node characterization / authoritative fixture corrections |
| `0a33a10` | Shared Hello/framing/trust runtime; Windows Node adoption |
| `00e3522` | Android JNI reliable endpoint using existing native owners |
| `43deb15` | Native manager-owned relay attempts and positive JNI persistence |
| `c005647` | Authenticated server relay physical execution adapter |
| `c13d153` | Native-backed application snapshots and protected account/device index |
| `55188a6` | Actual isolated server / production JNI reliable messaging proof |

Approved separate integration branch `codex/android-messaging-integration`:

| Commit | Responsibility |
|---|---|
| `0a2cfd5` | Accepted UI import (`7f4fc70`), preserving native/TLS build configuration |
| `e945ba9` | Accepted UI tests/docs import (`1d7f3dc`) |
| `f5a00ed` | Normal native-backed presenter, authenticated friend controls, tests |
| `4a8087e` | Identity-preserving physical validation procedure |

The original Android backend remains at `55188a6`, source UI at `1d7f3dc`.
No merge into main or the authoritative Windows worktree. No phone/ADB command
was executed. Untracked generated ScreenMedia object files remain uncommitted.
The backend's unused earlier draft header is not included in production/checkpoints.

## 2–7. Runtime, Node, JNI and existing owners

`reliable_peer_protocol.hpp` is the common bounded PT1 framing / Hello encoding,
capability, identity, stored-pin and Accepted guard seam. `shared_peer_runtime.hpp`
is the common callback-driven receive/send plumbing. Node's Connection consumes
that shared state/runtime; Windows continues providing socket execution and
Schannel. Windows-specific group/media dispatch and UI trust decisions are not
duplicated into Kotlin or moved into a new owner.

Android's `portable/reliable_endpoint.hpp` supplies an execution adapter around
the existing opaque TLS provider, shared runtime and references to Profile,
Storage, SessionTransportManager and DeliveryManager. JNI handles are checked,
bounded and retired safely. `portable/relay_requests.hpp` exposes only physical
execution records for attempts scheduled by SessionTransportManager: no new
retry scheduler or session-selection authority.

SessionTransportManager still owns attempts, winning sessions, network revisions,
backoff and retirement. DeliveryManager/Storage still own text durability, outbox,
ordering, ACK and dedupe. Kotlin executes authenticated server requests, socket
reads/writes and immutable projection only. Android uses the SAME native queue,
Hello/trust/message/ACK implementation, not a Kotlin messaging engine.

## 8–11. Host protocol, direct/relay, reconnect and messaging proof

- 1,000 shared Hello/trust/Accepted lifecycles, malformed/identity rejection,
  Unicode and existing wire compatibility: PASS.
- 1,000 manager-owned relay request/retry lifecycles, timeout, stale network,
  current friendship/device binding and logout: PASS.
- 102 real Schannel/shared endpoint lifecycles: PASS. Existing durable message/ACK,
  disconnect-before-ACK same-ID resend, dedupe and retirement checked.
- JNI provider containment: 1,100 lifecycles including 100 concurrent; 1,000
  foundation Unicode/NUL/copied-buffer/invalid/stale cycles; 1,000 reliable
  invalid/unbound/stale rejection cycles: PASS.
- Positive production JNI + Profile + actual SQLite/DPAPI + pinned Java TLS:
  eight reopen cycles, history/ACK and lost-ACK restart: PASS.
- Actual authenticated PeerChat.Server opaque relay: three socket/profile reopen
  cycles, six bidirectional native durable text/ACK deliveries, exact history and
  unrelated account relay rejection: PASS.
- Committed/current Windows Node: six concurrent peers, 15 real mutual TLS links,
  30 text/ACK deliveries, trust/pins, restart/history and offline outbox: PASS.
- Windows server-first relay / asymmetric EOF / stale-session retirement,
  reconnect, optional P2P standby/promotion/fallback and existing recovery:
  PASS. No production retry assertions were weakened.

These are host/isolated-server results, not Android device messaging results.

## 12–13. Presenter and separate UI integration

Ordinary MainActivity uses NativePresenter. `MessagingApplication` is a narrow
immutable state/high-level command seam implemented by MessagingBackend.
Login invokes verified server TLS/authentication; conversations/history/delivery
are read from native owners. Send success means the native durable operation was
accepted, not a fabricated ACK. Queued/Sent/Delivered differ. Send failure retains
the composer draft. Older history pages are native read-only projections.

Authenticated friend request/accept uses current server APIs, and only refreshed
accepted friendship/device state authorizes native sessions. Initial peer pinning
uses the existing constrained authenticated friend rule. Device replacement
remains fail-closed; no trust-all or auto-replacement override was added.

Demo is explicit DEBUG intent only. Normal release/debug launch never falls back
to samples. Unsupported group/media/file actions are unavailable, not simulated.
Normal account identity/profile is `default`; passwords are not saved into UI
saved-state and no plaintext token persistence was introduced.

## 14. Current integration APK artifacts

Root: `C:\Users\Livel\.codex\worktrees\android-messaging-integration\Create 2D Game\peerchat`

| Relative path | SHA-256 |
|---|---|
| `android/app/build/outputs/apk/debug/app-debug.apk` | `1B5412107D8A5799EF63DD9B808D668E8544E5BF00F90DA27FBE382502BAB308` |
| `android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk` | `CA7825C1B126A7080DC0CE089023A338D0D121305E94B2B2891C36F98F7FCECB` |
| `android/app/build/outputs/apk/release/app-release-unsigned.apk` | `F61F8B7E209C444A243EDB98ACDA0DDBB5CB4794F3C63FB03367A96F5D5541A7` |

Release is an audited unsigned build, not a published release.

## 15–17. Audits, Windows regressions and full build

Android: 38 host JVM/Robolectric/UI/presenter/projection tests, zero failures;
debug + instrumentation + release APK build and lint: PASS.
ARM64 messaging/JNI exports and allowed ELF dependencies: PASS. No Windows DLL
imports. Destructive Storage test hook excluded from release. Only intended
native components, no credential/private-key/test-certificate fixtures packaged.
Pinned runtime is BC TLS1.86.1 plus bcprov/bcpkix/bcutil1.86; licenses retained.

Original-message signing / no double-hash / unchanged Windows verifier, 1,000
signing cycles (100 concurrent), actual JNI/TLS100 mutual loops, TLS1.2/1.3,
wrong pin/cert/time/provider/tamper rejection and Schannel interoperability: PASS.
Host Java22 BCJSSE warns about two unsupported legacy SHA1 constraint clauses;
no security configuration was relaxed. Android hardware behavior is not inferred.

Windows CNG/identity/DPAPI, portable production SQL/record providers, Profile32
reopens, history/outbox/ACK/dedupe, transaction rollback, corrupt/missing record
fail-closed and existing group AEAD/signature/tamper tests: PASS.
Isolated discovery/auth/friendship/presence/lease/revocation/relay/restart/admin
persistence suite: PASS.

Full Windows authoritative `build.ps1 -All` and package/import audits from the
integration worktree's own source: PASS, `build/development/4.0.1-dev.1/Package`.
Backend earlier full builds also passed through `4.0.1-dev.21`; those binaries
were not borrowed for clean integration validation. Third-party caches only
were reused; no WebRTC/libdatachannel/Opus/MbedTLS rebuild/download occurred.
`git diff --check` and staged checks: PASS. Existing compiler warnings remain.

## 18–20. Unproven device behavior, blockers and next procedure

Remaining proof requires the physical Motorola: current normal app/presenter,
protected friend metadata, application relay execution and Android↔Windows live
messages/ACK/reconnect/process restart. Earlier physical Keystore/Profile/TLS/
discovery evidence is not relabelled as proof of the new application path.

Known bounded limitations: session-local server configuration and explicit login
after restart, no device-replacement approval UI, no Android background/doze/push
guarantee, no account-creation UI, offline retained contact-list UI or group/media/
file transport. These are not hidden insecure fallbacks.

No known host-side security/persistence/regression blocker remains for the first
physical direct messaging validation. Read `ANDROID_MESSAGING_VALIDATION.md` and
run `tests/prepare-android-messaging-device.ps1` in the integration worktree when
the same phone returns. It replacement-installs only, checks the accepted public
identity and launches the normal app. Then authenticate, establish friendship,
exchange both directions, verify actual ACK/dedupe, restart/reconnect and exchange
again. Follow with `-Mode Collect` and correlate bounded error counts locally.

PHONE REQUIRED TO CONTINUE
