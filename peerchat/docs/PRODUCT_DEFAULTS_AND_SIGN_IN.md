# Product defaults and sign-in integration

## Product policy

Safe useful features default ON. Offer OFF only for a meaningful privacy,
resource, quality or workflow reason. Preserve explicitly saved OFF values;
absence is not an explicit choice. Unreadable protected settings fail closed.

Mailbox/server-assisted offline delivery, echo cancellation and eligible history
synchronization default ON. This never implies archive enrollment or device
ownership transfer. Same-account authentication is not archive permission.

## Implemented in 4.0.2-dev.53

* Windows missing mailbox policy defaults allowed; protected explicit FORBIDDEN
  remains forbidden. Invalid protected data remains forbidden.
* Android missing mailbox preference defaults allowed, including an older
  protected QoL record without that field. Explicit false persists; malformed
  or unverifiable protected records are not accepted as consent.
* Windows missing AEC preference defaults true. Explicit saved false survives
  reopen. Existing APM/render-reference implementation, processing order,
  packet framing, diagnostics and failure policy are unchanged. Android voice
  remains unavailable; no pretend Android AEC control was introduced.
* History participation is an account/device-bound protected record in existing
  native Storage. Missing defaults ON. Every archive authorization check
  consults this policy. OFF blocks serving and receiving, including existing
  admitted endpoints on their next authorization check. Grants, original local
  history, outbox, mailbox, files and stable resume checkpoints are untouched.
  Re-enabling never authorizes a device or changes grant generations.
* Windows Privacy & Security and Android You/Settings expose history participation
  alongside explicit enrollment. Refresh after authenticated sign-in loads the
  saved account policy. The local command binds account/authentication generation
  to reject stale Settings actions. There is no additional policy/retry owner.
* Sign-in exposes Create Account on both platforms. Android welcome also uses
  the real creation screen instead of its old placeholder. Back/Already-have-an-
  account navigation preserves endpoint, protected pin and existing identity.
* Android registration calls the established ServerAccounts.register operation,
  then the same presence/bootstrap/native application and optional Remember Me
  flow used by login. No alternate authentication or Kotlin messaging engine.
* Server device ownership remains unchanged. A registered device cannot be
  silently moved to another account. No trust mismatch can suggest creating an
  account as its fix. Errors are bounded and never include exception payloads,
  passwords, tokens, keys or messages.

The existing server deliberately combines nonexistent-account and wrong-password
responses as login_failed. The client explains that ambiguity rather than
inventing a distinction or adding account enumeration. Explicit server ownership,
registration, unavailable-server and certificate-mismatch results remain distinct.

## Scope still in progress

History policy is not a claim that full product archive synchronization is ready.
Native authenticated paging/import/resume and peer-TLS harnesses are validated;
production archive-purpose route execution, conversation discovery, provenance
projection, progress/cancel binding and TransferManager attachment integration
remain part of the ongoing archive task. There is no temporary plaintext server
archive or automatically enrolled account device.

Archive policy OFF rejects further work; it does not erase received history or
revoke grants. Production route/job teardown must use existing session/delivery
owners as that remaining integration is connected. No alternate retry loop.

## Validation

* Full Windows build and package/import audit passed for immutable dev.53.
  Dev.52 was an intermediate validation build and remains untouched.
* Android debug/test/release builds and lint passed.
* 104 JVM tests passed, including missing/explicit-OFF protected mailbox records,
  Create Account navigation, preserved endpoint/pin, established application
  command dispatch, no fake demo authentication and ownership-safe error text.
* Native enrollment, protected policy reopen, unknown authorization denial,
  history OFF/preserved eight imported records/stable-cursor resume, and 12
  real mutual Schannel archive sessions passed.
* Packaged native application tests passed across five profile-worker restarts,
  including stale generation rejection for policy changes.
* Packaged WPF Settings smoke passed for mailbox defaults/OFF/corruption and
  no implicit archive grants, both account-navigation controls, and specific
  device-ownership/TLS/post-authentication startup errors.
* Voice preference/APM/framing and 100 receive lifecycle regressions passed.
* Mailbox encryption/permissions/receipts/offline sender and remembered login
  signing/replay/revocation tests passed.
* ARM64/JNI/TLS dependency/license/release containment and dev.52-to-dev.53
  Development updater version/hash/extraction audits passed.

Final Windows Development ZIP:
`build/development/4.0.2-dev.53/DevelopmentUpdater/PeerChat-4.0.2-dev.53.zip`
SHA-256:
`d13cb8d912144ad9e637c3733f226734647c4f39e41d078f2524f2d549f73c5e`.
Unsigned Development preparation only; nothing published.

No phone install, user-data clearing, normal identity/key replacement, merge or
publication. Pi server dev.46 remains RUNNING unchanged on 7443; coturn and external
FCM configuration are untouched. Physical new defaults/sign-in/archive acceptance
remains pending.
