# Archive enrollment Settings and authenticated public RPC — dev.51

Windows Settings → Privacy & Security → Archive devices and Android You →
Archive devices now use the native Enrollment owner through the existing profile
worker/JNI gate. A bounded selector displays current account device Peer IDs.
Refresh registers only the exact durable self-signed public descriptor; it does
not approve an issuer or grant archive access.

Trust authorizer, authorize receive/serve, revoke local-issued access and disable
authorizer all require explicit confirmation showing account and target Peer ID.
Local application operation 10 binds consent to the displayed account and
authenticated generation. Native policy rechecks fresh current metadata and the
exact context before mutation; stale views cannot authorize a replacement login.
Android consumes application snapshots/commands, not network or grant internals.

The execution adapters use existing authenticated bounded RPC:
archive_descriptor_register, archive_grant_register and archive_authorization_state.
They own no socket, scheduler, history DB, signing policy or durable queue.
Native signatures and issuer approval remain authoritative. Server metadata is
validated natively; same-account login still grants nothing. Failed refresh
invalidates access, and publication ambiguity leaves signed intents durably
pending. At most four pending proofs are published per user operation, in order.
A monotonic process clock keeps metadata revisions increasing when Settings is
reopened without a new authenticated generation. There is no automatic retry loop.

No server code or deployment changed. The validated dev.46 Pi remains RUNNING:
PID 23441, 7443, /opt/peerchat-test/server/4.0.2-dev.46/PeerChat.Server.
Existing certificate, SQLite, TURN and FCM configuration remain untouched.
Coturn PID 2718 remained running.

## Validation

- Full Windows dev.51 build, package/import and restricted-load audits passed.
- Android debug/test/release builds, lint, ARM64/JNI/dependency audits passed.
- 100 JVM tests, zero failures/errors, including five RPC adapter cases and one
  Compose confirmation/cancel test; no identity/Keystore/device execution claimed.
- Native enrollment/provenance/history and 12 actual mutual peer-TLS archive
  harness handshakes passed, including bounded pages and revocation.
- Packaged C ABI: five profile-worker restarts, existing identity/descriptor,
  stale-generation consent rejection, ordered durable publication, ambiguous
  failure retry, idempotence, account mismatch and logout/fresh metadata passed.
- Packaged WPF Settings smoke renders controls and rejects unsigned-in refresh
  without RPC or implicit enrollment.
- Windows profile/identity/DPAPI/storage/history/outbox/ACK/dedupe regressions,
  remembered authentication and FCM provider regressions passed.
- Mailbox native/managed/TLS, sender-disconnect persistence, signed receipt,
  restart, quota and revocation regressions passed. An initial native run reported
  a generic CNG rejection; the identical failed executable passed on repeat with
  a fresh isolated fixture, and a complete independent rerun passed. No root cause
  for that transient rejection is claimed; no crypto assertion was weakened.
- Development version/manifest/hash/extraction audit passed under Windows
  PowerShell/.NET Framework. PowerShell Core cannot load the Framework updater's
  System.Web Utf16StringValidator; its failed audit artifact is preserved, not
  overwritten. No production updater change was needed.
- Validated ZIP: DevelopmentUpdaterFramework/PeerChat-4.0.2-dev.51.zip
  SHA-256: 004e7cddb3043e1a9ec1d055f8b2f8b665e5ddba7138ad063503e86530767e26.
  Unsigned Development preparation only; nothing published.

## Still pending

No actual user-visible history sync is claimed. Production archive-purpose route
execution, conversation discovery, provenance projection, sync controls and
TransferManager attachment integration remain pending. This Settings surface
explicitly says grants do not start synchronization. No phone install or physical
archive acceptance was performed. Earlier accepted client/media behavior remains
unchanged. Existing unrelated object files are excluded.
