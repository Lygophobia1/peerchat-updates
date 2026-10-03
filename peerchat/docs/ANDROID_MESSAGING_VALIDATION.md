# Native-backed Android messaging physical gate

## Accepted physical messaging milestone — 2026-10-03

The user has accepted the installed reconnect/configuration/UI candidate:
Android→Windows and Windows→Android text, restart/reconnect, queued/offline
recovery, unchanged identity, protected host/port/fingerprint persistence,
composer system/IME insets and real-presence indication. This acceptance
supersedes the earlier pending-physical notes below; those describe prior runs.
APK SHA256: `E2BF4B124FB26EA402B5AB5368036C1343CF68319A4FE57E19D6BF06109EE578`.
No additional phone/ADB testing is performed for the overnight continuation.
45 host/UI tests, server replacement/queued delivery/heartbeat expiry,
1,000 stale-attempt lifecycles, 102 shared/TLS lifecycles, Android builds/lint,
dependency/import audits and diff check are green. Remember Me, background
notifications and push are not included in this accepted milestone.

## Cold-start comparison with main reconnect fixes — 2026-10-03

Both `e303cd9` and `7075130` are ancestors of integration HEAD `62a028b`.
The server, SessionTransportManager and DeliveryManager sources match the
pre-Android main baseline `2a5b721`; no blind cherry-pick is needed.

- `e303cd9`: client heartbeat event leases expire after 75 seconds; a new
  account/device event subscription replaces the old token; superseded tokens
  cannot reclaim the subscription or refresh presence. Session-scoped logout
  cannot clear a replacement subscription. Android `ServerAccounts.openEvents`
  has the heartbeat capability, but normal `MessagingBackend` currently uses
  five-second authenticated presence/friend-state polling, not an event channel.
  This application-level difference is real, but is **not proof** of the physical
  cold-start failure: polling itself publishes presence successfully in the
  isolated server regression. Event-channel binding is not added speculatively.
- `7075130`: Windows Node previously skipped dead/auth-timeout transport cleanup
  in its relay-only idle branch. Android `ReliableBridge.requests` already reaps
  closed, unattached and 20-second auth-timeout endpoints before issuing owner
  requests. Endpoint retirement uses the existing session/delivery owners.
  Android has no Windows select/empty-socket early-continue branch to port.
- Relay session IDs are parsed from each fresh authenticated `relay_ready`
  response; they are not restored from disk. Native attempt IDs stay scoped to
  the profile runtime, and stale device/network/attempt bindings are rejected.

Confirmed application defects corrected narrowly: login alone previously
projected Online even after refresh failure; a server refresh exception aborted
that tick before native route retirement/event/ACK/retry servicing. The snapshot
now separately records `serverPresenceLive`, set only after validated presence,
friend-state reconciliation and native presence publication. Transient network/TLS
I/O refresh failure projects Reconnecting and retains/drains native state;
validation/provider failures are not treated as transient network failures. Terminal
`session_invalid`, `session_superseded`, and `event_session_superseded` revoke
authorization and require sign-in. Diagnostics include only safe error classes
or bounded server rejection codes, never request/exception contents or tokens.
There is no automatic credential persistence or invented active connection.

Host regression: actual isolated Server + pinned outer TLS + production
JNI/Profile/shared peer TLS, three fresh-login/native-runtime reopen cycles,
old event-token replacement and rejection, fresh device presence observed by
the authenticated peer, six bidirectional messages and two offline queued
messages drained once with original IDs/ACK/dedupe. Existing eight-profile
reopen/lost-ACK test, 1,000 stale device/network/attempt/retry lifecycles and
102 Schannel/shared endpoint lifecycles pass. These are host/server evidence,
**not physical Android restart acceptance**.

Physical retest must distinguish returning to a background activity from a real
force-stop/process restart. A real cold start requires explicit sign-in (no
password/token persistence). After either return or sign-in, inspect diagnostics:
`Authenticated=true Server presence live=true` must precede Online. If it stays
Reconnecting, record the safe refresh rejection/classification. Exchange text in
both directions, then force-stop/reopen/sign in, observe Windows presence and
confirm queued delivery/history. The exact reported phone failure is not yet
proven resolved; preserve that limitation until the physical retest passes.

Current reconnect candidate: **45 host/UI tests, zero failures/errors**;
debug/test/release builds, lint and ARM64/JNI/dependency audits pass.
Discovery regression passes heartbeat expiry, fresh authenticated replacement,
session-scoped logout and server restart persistence. `git diff --check` passes.
Final candidate APK SHA256:
`E2BF4B124FB26EA402B5AB5368036C1343CF68319A4FE57E19D6BF06109EE578`.
Replacement-installed and normally launched on the same Motorola; existing
normal identity/reopen assertion passed. Bounded logcat linker/JNI/crash/SQLite/
Keystore/TLS error counts were all zero. No uninstall/data reset occurred.
Real Windows↔Android messaging after restart remains pending. Changes are not
committed or merged as accepted physical reconnect fixes.

## Final integration corrections — 2026-10-03

The user reports successful physical messaging for the preceding integration
build. The following new corrections still require replacement-install physical
acceptance; that earlier result is not relabelled as acceptance of these changes.

- The normal profile persists server host, port and trusted fingerprint together
  in `server-configuration.pcr`, using the existing Keystore-backed PCR1 provider
  and atomic app-private/no-backup storage. No password/token/private key is
  persisted. Loading uses the exact saved pin; malformed, tampered, missing-key
  or unreadable existing records block sign-in until the user explicitly saves
  independently trusted settings. There is no silent blank-pin fallback.
- Sign-out keeps server configuration; a new presenter/cold launch preloads it.
  Loading/saving occurs on IO dispatch, with sign-in blocked during either a
  pending save or invalid saved configuration. Failed saves retain the previous
  in-memory trusted configuration. Login does not display the fingerprint.
- Edge-to-edge layout is explicit. The composer owns the union (maximum, not
  sum) of navigation-bar and IME bottom insets. Scaffold reserves its height;
  content consumes Scaffold padding. No hardcoded navigation spacer.
- Online status/dot text is green (`#4ADE80`) only from the existing authoritative
  presence projection. Selecting a contact cannot turn offline state online.

Automated result: **43 host/UI tests, zero failures/errors**; debug/test/release
APKs and lint pass (0 lint errors, 15 warnings). ARM64/JNI/release dependency and
exact pinned TLS closure audits pass. Original-message signing, wrong-pin/
certificate/provider rejection, 100 mutual JNI TLS lifecycles, Windows verifier
and unchanged Schannel interoperability pass. `git diff --check` passes.

Previous configuration/UI candidate debug APK SHA256 (before reconnect changes):
`46F45D05B35A0B2028C63E0B964778075A5D74E975C3CAE781D5611F7FE8C682`.
Replacement installation passed on the same Motorola moto g power (2025),
Android 16/API 36, arm64-v8a, serial `ZA2235NTXZ`. The existing normal-profile
identity assertion passed and preserved Peer ID
`61f70fb01a6055499eed089025e8e8e2850d640b4c4a6ba3ad772168f092b51b`.
`ServerConfigurationTest` plus `UiSmokeTest`: **OK (8 tests)** in 19.728s,
including protected configuration reopen/tamper/truncation and composer editing/
presence color. Test records/aliases cleaned up; no uninstall/data clear occurred.
The normal app was cold-launched successfully after instrumentation.
These synthetic/UI checks do not establish live messaging or real presence.

Final live physical acceptance remains pending: saved configuration across cold
launch/login, actual online/offline presence, keyboard/nav-bar usability and
bidirectional Android/Windows message/ACK/history/reconnect. Use replacement
installs only; never clear data/uninstall.

After installation: save the independently verified server/fingerprint once,
sign in, inspect online/offline colors, type/send with keyboard open and closed,
exchange Android↔Windows messages and confirm real Delivered/ACK. Force-stop,
cold-launch, confirm unchanged identity/history and preloaded host/port, sign in
without re-entering server settings, and repeat bidirectional delivery. Also run
the new namespaced `ServerConfigurationTest` (32 reopens, tamper/truncation fail
closed, no extra identity) and UI instrumentation. Its cleanup touches test-only
records/aliases, never the normal identity/profile.

This integration branch is `codex/android-messaging-integration`. It imports the
accepted UI onto backend checkpoint `55188a6`; neither source branch nor the
authoritative Windows worktree is modified or merged. Normal launch uses
`NativePresenter → MessagingApplication → MessagingBackend → native Profile /
SessionTransportManager / shared peer runtime / DeliveryManager`.

Demo fixtures are reachable only with an explicit DEBUG launch intent
`--ez peerchat_ui_demo true`. Release and ordinary launches never fake login,
messages, ACKs, calls or voice rooms. Android group/media/file features remain
unavailable. Username labels use authenticated server friend/account data;
account/device IDs remain canonical internally.

## Host evidence (not phone evidence)

- Shared Hello/trust/framing tests: 1,000 lifecycles and malformed inputs.
- Native relay request tests: 1,000 owner-driven request/retry/stale lifecycles.
- Windows Schannel/shared endpoint: 102 connection lifecycles, durable text/ACK,
  disconnect-before-ACK same-ID retry and dedupe.
- JNI lifecycle: 1,100 provider loops (100 concurrent), 1,000 native boundary loops,
  1,000 missing-profile/stale reliable handle rejection loops.
- Production JNI/Profile/real SQLite/DPAPI and original-message TLS: eight reopen
  cycles, persisted outbox/history and lost ACK recovery.
- Actual isolated PeerChat.Server: three authenticated opaque-relay reconnects,
  six bidirectional text/ACK deliveries, native history and nonfriend rejection.
- TLS: original-message signing, no double hashing, P-256/raw64 Windows verifier,
  TLS1.2/1.3 mutual handshakes, wrong-pin/certificate/provider rejection, unchanged
  Schannel interoperability. Java22 host warns about two unsupported legacy SHA1
  constraint clauses; no security configuration was relaxed.
- Native-backed presentation projects immutable native snapshots and delegates
  commands. Queued, Sent and Delivered remain distinct. Failed native send keeps
  the composer draft. Older pages come from native Storage, not a Kotlin database.
- Server friendship request/accept controls use existing authenticated APIs; only
  subsequent authoritative friend snapshots authorize native peer sessions.

## First physical message — one preparation command

In the integration product directory, after reconnecting the SAME Motorola phone,
unlock it, enable/authorize USB debugging, and run:

```powershell
.\tests\prepare-android-messaging-device.ps1
```

The script detects SDK adb if absent from PATH, confirms the known serial and
ARM64, replacement-installs the current debug and instrumentation APKs, runs the
current normal-profile/identity assertion, checks the known public Peer ID and
launches the **normal** app. It never uninstalls, clears app data, deletes keys or
creates another visible application. No password or session token is requested
on the command line. A preparatory PASS is not a messaging PASS.

1. On Android, open Advanced / server settings. Enter the same server as the
   Windows client. Leave pin blank for system CA+hostname validation, or enter
   an independently verified exact SHA-256 certificate pin for the development
   server. Never accept a pin copied from an unauthenticated connection.
2. Sign in to the existing Android account (do not create another device identity).
   Password is not saved into Compose saved-state. The same native profile and
   Keystore identity must be shown in You / application diagnostics:
   `61f70fb01a6055499eed089025e8e8e2850d640b4c4a6ba3ad772168f092b51b`.
3. Establish account friendship using You → Add friend by username and the
   existing Windows request controls, or accept a Windows-initiated request in
   Android You. Only accepted server friendship enables the relay session.
4. Open the direct conversation. Check native diagnostics: Server Relay,
   Ready=true, Pending=false after peer TLS/Hello/trust. A device replacement
   remains fail-closed; do not bypass a verification-required notice.
5. Android → Windows: send a unique harmless Unicode test message. Verify one
   receiver entry and Android Delivered only after the actual ACK.
6. Windows → Android: send another unique message. Verify one Android entry,
   correct display attribution and actual Windows delivery ACK.
7. Disconnect/reconnect network, then exchange again. Confirm native retry
   state, no duplicate history entries and original order.
8. For pending delivery, disconnect the recipient, send on Android and verify
   Queued/Sent (not fabricated Delivered). Stop the Android process, relaunch and
   sign in; reconnect the recipient. Verify one delivery, ACK and persisted history.
9. Force-stop/cold-launch the installed package several times without data clear.
   Sign in and verify same identity/profile/history; exchange a second message pair.
10. Run `prepare-android-messaging-device.ps1 -Mode Collect`. This saves bounded
    recent-device error **counts**, not raw logs/messages/secrets. Correlate any
    count with the package/time in local logcat before reporting it as a PeerChat
    error. Zero counts alone are not proof of success.

For the already-established namespaced storage/TLS restart suite, optionally run
`tests/test-android-production-restart.ps1 -Serial ZA2235NTXZ`. It cleans only its
own test profile. Do not run arbitrary destructive tests against `default`.

## Remaining limitations / honest boundary

The new live application execution, protected friend metadata index, presenter,
friend controls and Android↔Windows messaging have NOT been physically exercised
in this no-phone pass. Prior Keystore/Profile/TLS/discovery physical evidence is
not re-labelled as validation of these changes. Host command fixtures do not
prove Keystore scheduling on Android.

Server settings now persist in protected app-private storage; sign-in is required after process restart;
no plaintext token/password persistence is added. Normal app has one `default`
profile/identity. Historical device replacement still requires explicit trusted
verification; this checkpoint does not add an insecure auto-repin UI. No Android
background-service/doze/push guarantee, offline retained contact-list UI, group
transport, media or file transfer is claimed.
