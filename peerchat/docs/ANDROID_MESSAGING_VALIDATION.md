# Native-backed Android messaging physical gate

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

Server settings are session-local and sign-in is required after process restart;
no plaintext token/password persistence is added. Normal app has one `default`
profile/identity. Historical device replacement still requires explicit trusted
verification; this checkpoint does not add an insecure auto-repin UI. No Android
background-service/doze/push guarantee, offline retained contact-list UI, group
transport, media or file transfer is claimed.
