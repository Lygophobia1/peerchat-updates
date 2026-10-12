# Current-session group device admission — dev.114

Adds internal, default-inactive application prerequisites for device possession
publication/readout and signed same-account group approval/revocation. No new RPC,
UI action or production enrollment switch is enabled.

Every prerequisite checks the current server presence epoch and lease generation,
live account/device mapping, exact authenticated session hash, session expiry or
revocation, and remembered-device binding before invoking the existing signed
proof/database operation. Stale captured leases cannot authorize a replacement
session. A new server epoch cannot reuse a pre-restart context. Account login alone
cannot substitute for the device's signed identity proof or signed group event.
Normal authentication, presence, messaging, History Sync and transport ownership
are unchanged. No secret/token values are logged by these checks.

Windows and portable-server tests passed for missing presence, wrong account,
wrong device/session, invented generation, superseded lease, remembered-session
device substitution, revoked/expired session, disconnect, fresh reconnect, server
restart epoch and explicit reenrollment. Rejected attempts leave the group roster
unchanged. Existing native CNG/portable approval, revocation, receipts, rollback,
restart and bounded evidence tests remain green.

Remembered-login regressions passed (32 resume cycles, replay/wrong signer/device
rejection, durable reopen and revocation). Presence passed 1,000 lease cycles;
composer and mailbox lifecycle tests passed, including 100 stop/drain/restart
cycles. The presence UI harness previously omitted the current attachment partial
class and used the legacy compiler. It now compiles complete existing application
sources using the same Roslyn compiler and updater dependency as the Windows
build. No assertions were weakened and no production UI change was made.

Full Windows build/import audit, packaged native group contracts, 127 Android
JVM/Compose tests, debug/test/unsigned-release builds, lint, ARM64/JNI/dependency
audits, version/Development updater extraction/runtime hashes and git diff
--check passed. Existing compiler/deprecation warnings remain; no new runtime
failure was reported by these tests.

dev.113 is retained as an unpublished intermediate candidate; dev.114 includes
the final epoch binding. No install, deployment, merge, publication or physical
acceptance is claimed. Android DM multimedia from dev.107–109 is preserved.

Remaining enrollment gates include native application replica reconciliation,
verified missing-epoch recovery, current key provisioning/revocation and explicit
same-account approval UI. Production enrollment stays disabled until they pass.

Windows package:
`build/development/4.0.2-dev.114/DevelopmentUpdater/PeerChat-4.0.2-dev.114.zip`

SHA-256: `19c97a80a36145e6d2de214d2bfa412d6a5b63f1cf33b8abf0e2bfd007b180e7`

Android packages: `build/development/4.0.2-dev.114/Android/`

- Debug: `6ea81e64483b2cdaf899ff2f24eb559ff864908939cf3437cdede9e38b05054c`
- Test: `e4fcac9fa788d8fe682b0a5cad85fbdbE66c6cd95aa9b356512033d5b6c9a8c5`
- Unsigned release: `9ced5a50755286e84d3454c396c5e0173264be831feb0a0304759e620c4b9ad0`
