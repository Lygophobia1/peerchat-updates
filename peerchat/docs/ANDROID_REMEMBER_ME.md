# Remember Me

The checkbox defaults off. Off persists no authentication credential. Saved host,
port and trusted fingerprint remain independent. On requires an explicit trusted
fingerprint; automatic authentication never silently switches to another server.

After explicit login/presence, enrollment signs a one-shot server challenge using
the existing opaque P-256 device identity. The original input is domain-separated
and binds action, account, device, random challenge and hash of the authenticated
session. Native Profile signs through the unchanged Keystore SHA256withECDSA
provider. No digest is fed into that provider and no private key is exported.

Server storage retains only a SHA-256 hash of a random 256-bit credential, bound
to the owned device/account. Resume additionally requires a fresh device signature
bound to the credential hash. Challenges expire after 60 seconds, consume once,
and have bounded per-device/global admission. Android validates the entire signing
input rather than signing arbitrary server-provided bytes.

The credential has no fixed expiry: explicit revocation, reenrollment, device or
account deletion invalidates it. Enrollment replacement revokes old bound sessions.
Resume creates an ordinary expiring server session with an explicit device binding;
it cannot claim another device for presence, events or relay. Existing Windows
account sessions and Schannel behavior are unchanged.

Android stores endpoint, fingerprint, account, device and opaque credential as one
PCR1 authenticated, atomic, profile-local no-backup record using the existing
Keystore record provider. Passwords are never saved. Corruption/missing key fails
closed. An authenticated tombstone clears the record without backup resurrection.

Logout marks revocation pending durably before network work. If unreachable, that
record never auto-logins; future launch retries signed revocation and clears only
on acknowledgment or already-revoked response. Server revocation retires affected
existing relay/event executions through their current owners. Normal UI closure
does not revoke Remember Me. Authentication alone still projects Reconnecting,
not Online, until a successful authoritative presence refresh.

The credential is stable across resumes, intentionally avoiding a lost-response
rotation that could strand an atomic client/server credential pair. Reenrollment
rotates it; stolen credential bytes alone cannot sign a resume. Stronger account
credential-reset/admin-device-management UI remains a separate product surface.

## No-phone validation

- `tests/test-remembered-login.ps1`: production server dispatcher, CNG original
  signing, wrong signer/device/replay, 32 resumes, durable DB reopen,
  replacement/revocation and unchanged Windows account-token behavior.
- Android protected-store tests: 100 reopens, encrypted content, pending revocation,
  authenticated tombstone, tamper/truncation/context mismatch and unpinned rejection.
- Presenter tests: checkbox delegates high-level command; remembered authentication
  does not report Online before owner-supplied presence.
- All 56 Android host/UI tests, debug/test/release APKs and lint pass.
- ARM64/JNI/TLS release audit, shared profile/reconnect/queued-delivery and existing
  server group fanout/room tests pass. Full Windows build/package passes at local
  integration build `4.0.1-dev.4`; rollback Windows dev.64 is untouched.

Expired access sessions may perform one bounded remembered authentication refresh
through the existing coordinator. Superseded sessions never fight the replacement.
Generic session deletion is not broadened into credential deletion: credential
revocation is explicit on device-bound logout, reenrollment and account/device
deletion. No new administrator credential-reset UI is included.

Physical Keystore enrollment, process/reboot resume, offline logout retry and
bidirectional interoperability still require the morning device test.
