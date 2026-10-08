# Android sign-in/network recovery — dev.62

## Evidence and bounded correction

The Motorola was authorized in ADB and running dev.59. The user reported
`Connecting…` on the Sign In button, not a healthy online session. Available
startup logs reached native profile-open return but did not establish the exact
subsequent failing operation. No native deadlock, server defect or certificate
failure is claimed from those logs.

A deterministic presenter defect was reproduced: after setting `loginBusy`,
local-history restore could throw outside the remembered-authentication
`finally`. The outer handler then left the sign-in button disabled indefinitely
and incorrectly treated a local startup failure as invalid server settings.
The outer unwind now always releases busy state. Protected-settings verification
failure remains fail-closed and is distinguished from local messaging startup.

## Recovery ownership

`MessagingApplication.resetNetworkSession` delegates through the existing runtime
lease to the one `MessagingBackend`. It interrupts its tracked server sockets
before waiting for the coordinator lock, retires transient authorization through
the existing native friends/session boundary, closes event/RPC/relay execution,
clears transient retry state and projects retained local history offline.
It does not create another session owner, transport or reconnect scheduler.
The current foreground/wake operation deadline is retained.

Reset is available on Sign In, startup, and Settings. Disconnect and change server
uses the same reset before making settings editable. No protected identity,
recipient key, history, contacts, archive grants, trust pin or remembered credential
is deleted. The local authenticated session is discarded; unavailable remote
sessions expire under existing server lease policy, not an invented server reset.
Manual Sign Out retains its separate credential-revocation behavior.

A 30-second foreground startup watchdog ends an unusable partial startup through
the same reset. Automatic remembered resume is suppressed until explicit sign-in
in that process. Online still requires authenticated live server presence.
Canceled startup/login results and old-generation foreground/archive results
cannot restore the current startup/error UI. A bounded in-memory diagnostic
history retains startup/recovery/archive categories; no media, credentials,
message contents or private key bytes are recorded. Existing persistent records
are untouched. Local identity-read and history-projection stages have safe timing
breadcrumbs for the physical retest.

## Validation / physical acceptance

JVM regressions cover local-history exception unwind, authenticated startup
without live presence, canceled remembered startup, reset without sign-out or
history/pin loss, verified settings save after disconnect, and enabled Compose
recovery controls while Sign In is busy. Existing trust mismatch, protected
settings, Remember Me, scheduler, history and UI suites remain required.

Build debug, instrumentation and release APKs; run lint, JNI/ARM64/dependency
audits, the immutable Windows dev.62 build/package/import audit, version audit
and `git diff --check` before installing. This checkpoint changes no Pi/server,
TURN, Windows media, certificate or TLS verification behavior.

Replacement-install only (`adb install -r`). Compare non-secret hashes of the
existing identity, protected server/remembered records and recipient-key registry;
never print their contents. Physical acceptance remains separate from host tests:

1. Open the updated app: reach Online or a clear recoverable sign-in failure.
2. If stuck, select Reset Network Session; verify Sign In is usable.
3. Disconnect and change server: settings are editable without deleting data.
4. Verify the existing endpoint/pin, sign in, then cold launch Remember Me.
5. Verify the same identity/history, working FCM, and no stale current error.

Do not claim physical login/FCM acceptance from automated tests or installation.

### Automated results, 2026-10-08

- 110 Android JVM/UI tests: zero failures/errors. Debug, instrumentation and
  release APK builds and lint passed against final source.
- Existing real-socket ServerDeadlineProof: three blocked TLS reads canceled,
  workers exited, scopes failed closed and reopened; one provider initialization.
  The independent invocation uses binary BC JARs, excluding source archives.
- Debug/release ARM64, JNI exports, destructive-hook exclusion, packaged fixture/
  credential exclusion and exact pinned BC dependency audits passed.
- Full immutable Windows dev.62 build and package/native import/load audits passed.
- Android/Windows/generated version and dev.61-to-dev.62 updater extraction/hash
  validation passed under Windows PowerShell (.NET Framework). PowerShell 7 could
  not run the Framework manifest parser; no updater production code was changed.
- DevelopmentUpdaterValidated/PeerChat-4.0.2-dev.62.zip SHA-256:
  `9791659ff8f12ac1ed144e379f164c48aadf39ff92731501fd86121fd3681374`.
  Development-only unsigned artifact; nothing published.
- Additional legacy test-reliable-profile-jni.ps1 could not compile because it
  omits the current mbedTLS header dependency (mbedtls/sha256.h). No native code was
  changed here. This is not recorded as a passing native host regression; its
  socket-cancellation proof was run independently as described above.
