# Android connection reliability dev168

Dev168 fixes two reproducible client lifecycle defects and adds bounded diagnostics for the distinct eligibility, relay and file-transfer failures reported on the Motorola. It is a physical test candidate, not acceptance of all reported networking behavior. The live Pi and phone application data are unchanged.

## Confirmed defects and corrections

### People search could remain busy indefinitely

Cancellation in the current presentation context could leave `Searching` set forever. A replacement search could also wait behind a non-cooperative lookup's mutex without a UI deadline. The contacts presenter now bounds the entire UI operation, including debounce and mutex wait, to 12 seconds, invalidates late results and exposes retryable failure. It preserves serialized network ownership and account/query revision checks. Two focused reproductions failed before the fix; 33 contacts/presenter tests passed afterward. Checkpoint `a0f06b0` contains this change.

### Event writes could block the messaging owner

The long-lived event stream disabled idle read timeouts, but its heartbeat/group writes had no independent write deadline. Those writes ran while `MessagingBackend.tick` held the application owner lock. A stalled write could therefore delay other application work indefinitely. A bounded output adapter now performs the same event writes outside that lock, with an independent 10-second socket-close deadline and the existing 32-frame queue bound. Ambiguous writes are not replayed on the socket; existing native durable state owns reconnect recovery. Checkpoint `9c965d2` contains the adapter and focused tests. See `ANDROID_DEV167_EVENT_OUTPUT_TIMING.md` for the exact boundary.

The physical snapshot does not establish that a socket write stalled during that run. Other bounded RPC stages still share the existing owner; this repair does not claim to make every operation concurrent.

## Stage diagnostics and unchanged security checks

The application exports process-local last/max time, calls and failures for owner wait, ticks, contacts, groups, mailbox, eligibility, archive, native relay and projection. Relay servicing receives a fresh monotonic timestamp after preceding work; refresh/retry deadlines use completion time rather than an old tick-start time.

Eligibility validation now identifies the exact failing native boundary: account, friend generation, local device, snapshot format, future issuance, expiry, candidate ordering, binding format/signature/authorization, trailing bytes or sequence rollback/conflict. It includes native UTC comparison timestamps and validation cost, but no keys, credentials or message content. All original fail-closed checks remain. The supplied physical exception does not yet identify which check failed. The reported phone clock, 02:13 local with automatic time enabled, agrees at minute precision with the observed server time; smaller skew or another rejection remains possible.

Transfer diagnostics retain fixed operation/exchange stage, reason, input length and processing duration, including failures before an attachment row exists. Empty profiles sample these rows at most once per 10 seconds; profiles with attachments retain the existing one-second cadence. The total native diagnostic record remains bounded to 64 rows. No protocol, chunk/window preference, speed limit or status ABI changes are introduced.

## Read only Pi findings

The running server was dev166, PID 19692, using `/opt/peerchat-test/config/4.0.2-dev.166/server-config.json`. Load was 0.02/0.06/0.07 with about 3.55 GB available memory; these observations do not indicate resource saturation. No service, cache, database, identity or configuration was changed.

Phone archive attempts 14, 20 and 24 claimed the reader role for source device `35049f…8201b`, never acquired a source attachment and timed out. Their server ownership was released. Later attempts 29 and 32 also released ownership on disconnect before pairing. This is a source-pairing failure, not evidence of rejected history grants or a stale winning attachment. The Windows device active in other logged traffic was `0cb421…c9ee4`, not the requested same-account source. Source profile availability must be confirmed in physical testing; the repair must not substitute another device identity to force pairing.

DM, file and archive paths use authenticated peer TLS over the opaque PeerChat TCP/TLS relay. These observations do not concern WebRTC UDP/TURN media transport.

## Transfer reproduction boundaries

An isolated test connects the production Android Java TLS/context and JNI original-message signer to the Windows Schannel endpoint. Both endpoints run the production ReliableEndpoint and TransferManager, complete mutual Hello/trust/winner selection, negotiate 64 KiB chunks and 16 outstanding chunks, and simultaneously transfer 2 MiB plus 123 bytes in each direction with the existing 10 Mbps fixture cap. Exact committed bytes, integrity and completion ACKs pass.

The final fixture uses the shipping Windows file source on both endpoints, including its strict 32 KiB read limit. TransferManager intentionally splits a negotiated 64 KiB payload into 32 KiB source reads; that adapter limit is not a demonstrated defect and is unchanged. The fixture does not execute the Motorola's POSIX storage, Android content-provider offer adapter, Wi-Fi or live Pi pairing. It does not reproduce or establish the root cause of the reported physical Android-to-Windows failure.

## Validation

- Full Android host suite: 260 tests, zero failures or errors. Lint: zero errors, 25 warnings. Log: `build/dev168-android-regressions.log`.
- Focused new event-output, write-deadline, owner-timing, archive-wake and backend-lease tests: 13 passed.
- Native messaging durability audit: passed, including exact eligibility rejection stages, valid replay and retained valid cache after rejected input. Log: `build/dev168-eligibility-boundaries.log`.
- Archive/mailbox receipt integration: passed nonempty durable import, signed receipt only after actual mailbox receive, restart/replay deduplication and tamper/account/content rejection. Log: `build/dev168-archive-mailbox.log`.
- Authenticated transfer interop: passed. Evidence: `build/tests/transfer-auth-interop-ce0daefeb6994db7b7c0b18d2effd393`.

- Production JNI profile proof: passed eight durable reopen/ACK and lost-ACK restart cycles. New assertions cover readable diagnostics during attachment initialization failure, oversized input rejection without private exception text, unchanged two-byte status and retained active winner. Evidence: `build/tests/reliable-profile-jni-0c594069e04d40ba80c3213315037d34`.
- Reliable endpoint: passed 102 real Schannel/shared-runtime lifecycles, durable text/ACK/deduplication and bidirectional binary transfers. Log: `build/dev168-reliable-endpoint.log`.
- Mailbox protocol and Windows mailbox/eligibility integration: passed authenticated store/fetch/ACK, restart, revocation, independent recipient receipts, unknown unpinned device exclusion and bounded retry/fairness. Logs: `build/dev168-mailbox-protocol.log`, `build/dev168-windows-mailbox.log`, `build/dev168-windows-eligibility.log`.
- Archive security fixtures passed provenance, protected unresolved records, nonempty pages, mutual TLS, revocation, isolated purpose/winner and restart/replay checks. The initial wrapper invocation combined incompatible `Provenance` and `ServerMetadata` switches and exited after its native suites passed; the server-metadata/authorization mode was then run separately and passed. Logs: `build/dev168-archive-security.log`, `build/dev168-archive-server.log`. The latter includes genuine local authenticated relay pairing both ways, cancellation/retry, duplicate rejection without winner replacement and real supersession rejection. No live Pi changes were involved.
- Final Windows all-component build and Android ARM64 debug build passed. Final Android host/lint run also passed: `build/dev168-android-final.log`. Windows native dependency and package source/version/signature/ZIP audits passed: `build/dev168-package-audit.log`.

No physical acceptance is inferred from automated fixtures. The exact phone eligibility rejection and physical transfer failure remain unresolved until the new stage diagnostics are captured.

## Checkpoints and packages

Branch: `codex/native-unicode-emoji` in the existing `native-unicode-emoji` worktree. Starting checkpoint: `d30b391`. Logical checkpoints: `a0f06b0` search, `9c965d2` event output, `12b8407` authenticated transfer/JNI fixtures, `224143c` owner/native diagnostics, and `cb7e9bd` dev168 version. The package source checkpoint is `cb7e9bd42efe219211042937070702d7d37cd075`; subsequent documentation does not change package production source. Earlier accepted packages are preserved.

Relative to the repository root:

- Windows ZIP: `build/development/4.0.2-dev.168/PeerChat-Windows-4.0.2-dev.168.zip` (12,334,500 bytes), SHA-256 `85fd1dce2e99c3b3e9d3af34ed443d8ea76a5eb89bae5cdbd628d4fc62a99737`.
- Android APK: `build/development/4.0.2-dev.168/Android/PeerChat-Android-4.0.2-dev.168-debug.apk` (26,758,326 bytes), SHA-256 `2a3846301e06f8c88692b5a6b4e2e3aa6e282c1cf7a20ff95f295ecd44bef59e`.
- Audit: `build/development/4.0.2-dev.168/messaging-repair-package-audit.json`. APK signing SHA-256 remains `db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`.

No deployment or installation was performed. Build-generated screen-media object files were preserved under dev168's ScreenMedia output directory rather than committed as source.

## Physical acceptance checklist

1. Replacement-install the supplied APK without uninstalling or clearing data; open the matching Windows package with its existing profile.
2. Confirm connection status and send one text each direction. Restart Android and check queued delivery and duplicate suppression.
3. Search for a known contact. Confirm a result or actionable bounded error, never an indefinite spinner.
4. Send a small image and a multi-megabyte file Android to Windows; verify the completed file. Repeat Windows to Android without changing transfer settings.
5. With the existing authorized Windows PHONE source profile actually online, retry History Sync and inspect durable imported counts, not merely transport connection.
6. Copy full Android diagnostics after any failure. The new native eligibility, transfer stage and owner timing rows distinguish failures without permission or identity changes.

Stop for these physical results. Do not deploy to the Pi or begin unrelated Android features.
