# Enrollment-authority integration — dev.185 validation record

Baseline: `3c80c6c`, existing `codex/native-unicode-emoji` worktree. No Pi deployment is authorized for this checkpoint. This report distinguishes implemented interfaces, isolated validation, and outstanding live acceptance; it is not a claim that the existing Pi supports the extension.

## Trust and compatibility contract

- A device publishes a signed possession proof for its exact account, existing public identity, and pinned server. Publication does not establish contact trust.
- An account owner explicitly chooses one existing device as its enrollment authority. A contact (or another device on the same account) explicitly verifies that authority's complete fingerprint once through a separate trusted channel.
- The authority can endorse an exact registered device after explicit fingerprint confirmation and a fresh account/session/revision-bound server challenge. Endorsed devices cannot delegate.
- Clients verify signatures, exact key fingerprints, current authenticated account mappings, directory revision, target/root revocation, and session generation. Unknown roots remain untrusted. Missing devices remain part of the required cache audience; they are never silently dropped.
- The signed identity directory is supplemental evidence, not an authorization grant. Cache HPKE, membership, historical access, storage permission, ciphertext verification and durable receipts retain their existing checks.
- Native identity resolution uses short-lived, generation-bound leases (at most 60 seconds from the snapshot request start, also bounded by monotonic time). A response older than 30 seconds is rejected. Restart requires fresh authorization but retains explicit fingerprint consent in encrypted profile-bound storage.
- An unchanged directory does not rewrite its protected record. Revocation tombstones survive restart. Identity evidence is stored separately from message history and the recipient-key journal.

## Shared integration manifest

| Component | Change |
| --- | --- |
| `device_identity_endorsement.hpp` | Canonical, bounded authority-selection proof and unsigned challenge parsing; existing identity signing operations only |
| `device_identity_directory.hpp` | Protected explicit trust/revision state, idempotent revocation, no unchanged-snapshot write |
| `device_identity_application.hpp` | Shared typed native commands, scoped signing, verified directory reconciliation and key resolution |
| `windows_mailbox.hpp`, `node.cpp` | Local mailbox operation 14; scoped directory key fallback for cache evidence without overwriting normal contact pins |
| `portable/peerchat_core.cpp` | Android JNI identity adapter and the same verified key-resolution policy |
| Windows `DeviceIdentityDirectory.cs`, `DeviceManager.cs` | Authenticated server adapter, bounded background refresh, explicit authority/target confirmation UI |
| Android identity execution/Settings/storage adapters | Existing worker ownership, separate profile-protected directory database, explicit trust actions and nonblocking refresh |
| Server `DeviceIdentityEnrollment.cs` | Durable public proofs and short-lived challenges; device-bound enrollment/snapshot routes; current authorization and atomic revision checks |
| Server/client RPC boundaries | Bounded directory responses; no change to transfer frames or message ciphertext |

The additive server operations are `device_identity_register`, `device_identity_snapshot`, `device_identity_authority_challenge`, `device_identity_authority_commit`, `device_identity_endorse_challenge`, and `device_identity_endorse_commit`. The response contract is version 1. Existing servers report unsupported; clients retain their previously verified paths and do not manufacture trust. Old clients do not consume this directory automatically.

An older device that has not published the new possession evidence must connect using compatible code, or remain subject to existing explicit peer verification. Its registration alone is not a cryptographic endorsement. No keys or device registrations are reset to make tests pass. Authority replacement/recovery and recursive delegation are deliberately not implemented.

## Review corrections

1. Bind freshness to the start of the authenticated snapshot request, not the later UI/native application time.
2. Advance the server directory revision atomically with a new proof; do not advance it for a retry.
3. Treat independently re-signed identical possession payloads as idempotent, retaining the original proof; ECDSA signature bytes need not be deterministic.
4. Reject malformed refresh evidence without retaining the previous native lease.
5. Keep an unrelated unverified contact from aborting discovery for other contacts.
6. Provide a distinct explicit action for a second device to verify its account's existing authority; never silently select or replace that authority.
7. Keep enrollment evidence supplemental: a missing/unconfirmed authority must not block a cache audience already covered by existing verified pins. Final native audience admission remains authoritative. An inactive authority is still submitted to native so its lease is invalidated immediately.
8. Preserve Windows' exact uppercase TLS/session pin while using the canonical lowercase representation in signed identity records. The actual application regression exposed this boundary; the adapter regression now covers it.

## Validation record

- Native signed proof / portable parser / protected-directory application fixtures: passed. Evidence: `build/tests/device-identity-directory-377068e5c5fd489683a75da52b144f34`. Covers explicit bootstrap, offline target key resolution, conflicting/stale evidence, restart, generation changes, repeated revocation, and no existing-contact mutation.
- Windows native + WPF + screen-media validation build: passed in `build/tests/identity-integration-windows-20261018`. This is an intermediate validation build, not the release package.
- Final Windows `4.0.2-dev.185` full build and package audit: passed. Existing compiler warnings remain; no build errors. Group voice and screen-share C ABI lifecycle checks (15 each), real audio receive dependency, HPKE and restricted DLL-loading checks passed.
- Actual dev.185 Windows application enrollment/restart test: passed. Evidence: `C:\Users\Livel\AppData\Local\Temp\PeerChat-attachment-device-56e76b6bf8dc419e942c83b674e92bc5`. Uses the production managed client and native operation 14 against pinned TLS; covers explicit own-authority setup, protected consent persistence, fresh directory after restart, remembered credentials, replay and revocation rejection, and unchanged messaging session.
- Group-room real authenticated TLS event regression and group voice-session regression: passed. Covers reconnect, membership removal, stale signaling, one-member rooms and retired callbacks. Not a claim of physical audio acceptance.
- Android voice-enabled x86_64 app, instrumentation APK, all 473 unit tests and lint: passed. The isolated `com.peerchat.android.voiceintegration` package was installed only on `emulator-5580`. This is not physical Motorola acceptance.
- Server enrollment suite: passed with real pinned TLS RPC and ordinary TLS, bounded 32-device snapshots, rejection of a 33rd registration, pending-challenge restart, committed retries after restart, re-signed possession idempotence, stale/revision rejection, contact isolation and root/target revocation. Evidence: `C:\Users\Livel\AppData\Local\Temp\peerchat-identity-fixture-13`.
- Windows production cache regression: passed using the Windows Framework server candidate with its existing runtime configuration. Evidence: `F:\PeerChat-isolated-cache-tests\dm-application-a49f915331a64a2284d6e9df0df74482\fixtures-configured`. Covers two independent recipients, sender logout, server and client restart, lost append/complete responses, exact decrypted payloads, no early ACK, and signed receipt aggregation. Its preexisting key fixture is not the new directory-backed interoperability test.
- Earlier test attempts failed before validation: an intermediate portable TLS configuration was unsuitable for the Windows fixture; a bare Framework executable lacked its existing `.exe.config` needed for volume-path handling. Final production certificate loading and storage security were not weakened. The configured Framework rerun passed.
- Cache-purpose isolation regression: passed (`build/tests/cache-scope-c8f3d6fd4846455fa2fcb04916a1f565`), including 256-MiB plaintext/AEAD bounds, DM/archive isolation, legacy-journal rejection, encrypted-object TransferManager round trip and separate plaintext verification.
- Final ARM64 voice-enabled dev.185 APK: built successfully. Existing deprecation warnings only. Version, ARM64 ABI and the accepted signing-certificate SHA-256 were verified; no phone installation.
- Directory-backed Android emulator ↔ Windows cached-transfer integration: **passed**, final exact-source run 40.61 seconds, instrumentation `OK (1 test)`, driver exit 0. Evidence: `F:\PeerChat-isolated-cache-tests\dm-emulator-10b6ed52cf334524b92c860044a4c06b`. Real Android JNI/POSIX/Keystore and Windows native cache/HPKE controllers; real pinned-TLS server requests through the managed fixture proxy. Android pins only the first Windows device through live TLS; its second authorized recipient stays without a live contact pin and is resolved through the explicitly trusted authority and signed endorsement. Both recipients decrypt the Android-origin file; the reverse Windows-origin file is decrypted on Android. Exact Unicode basenames and binary contents, durable signed notice ACKs, and sender validation of signed saved-file receipts passed. The final fixture captures request freshness before the snapshot RPC, as production does; the server shut down gracefully after the run.

That interoperability fixture seeds isolated account/session state (not the live `codex` and `codex1` registrations); the server still validates every request. Android's native commands are real, but server RPC is proxied by the managed test host, **not** the Android `ServerAccounts` network worker. The separate production Windows enrollment test covers real login/remembered-session creation. These complementary tests do not establish full live-account or physical group-audio acceptance.

Fixture repairs matched production rather than relaxing it: restore the test actor's exact seeded session identity after generic account-primary `session_info`; use a separate protected directory database to avoid a nested transaction in the recipient-key lease; use the existing protected F: test volume; and select the explicit local cache-enabled server candidate. The gated standard server correctly returned `cache_feature_gated` and the unsuitable scratch volume correctly returned `cache_startup_space_protected`.

The emulator fixture's cross-process control clock is not a production network clock. Windows was about 0.2 seconds ahead of the emulator, so a Windows-stamped command could correctly fail Android's local freshness check before possession signing. Test control delivery accounts for this skew; production Android commands continue to use Android's own clock. No freshness rule was loosened.

## Checkpoints

- `3c80c6c`: supplied foundation baseline.
- `a81330b`: scoped native signing, directory resolution and native security regressions.
- `cca728a`: durable server enrollment, directory RPC and server regression coverage.
- `6d6ff90`: Windows/Android integration, explicit verification UI and client adapter tests.
- `8835173`: final directory-backed emulator/Windows cached-transfer regression and fixture boundaries.

Worktree: `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`; branch `codex/native-unicode-emoji`. All implementation and tests are checkpointed. The final documentation checkpoint follows `8835173`; its hash is provided in the handoff. Generated screen-media object files were retained under the dev.185 `ScreenMedia` build directory rather than left as untracked source. Existing packages and earlier checkpoints are preserved.

## Reproducible build boundaries

- Windows: `build.ps1 -All -DirectCacheCandidate` allocates a new versioned output; never overwrites an accepted package.
- Android emulator validation: voice enabled, isolated integration application ID, x86_64, pinned genuine NetEq under `build/tests/neteq-x64-ndk27`; unit tests, lint and both APK targets.
- Final phone candidate uses the existing ARM64 genuine NetEq dependency and existing debug signing identity. Do not install or change personal profiles as part of this checkpoint.
- No transfer wire-format, encrypted message format, media engine, cache quota, or cache authorization downgrade is introduced.

## Live boundary

The existing Pi is unchanged. Local fixtures are not the live `codex`/`codex1` registrations and cannot demonstrate that the older live dev.183 identity has consumed the new contract. Live bidirectional cached delivery through this new enrollment path requires a separately approved server rollout and legitimate one-time authority/device verification. Preserve all existing queued attachments while that boundary remains unmet.

## Candidate packages

Production source checkpoint: `6d6ff90` (native and server checkpoints above are ancestors). Test/documentation-only follow-up commits do not change these binaries.

Base directory: `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat\build\development\4.0.2-dev.185`.

- Windows: `PeerChat-Windows-4.0.2-dev.185.zip`, 12,547,495 bytes; SHA-256 `78c15ba10197a6f09efb797629105bf36cd0f938ec312a5da3f70ee80d49b7e9`.
- Android: `Android\PeerChat-Android-4.0.2-dev.185-debug.apk`, 28,582,588 bytes; SHA-256 `c47d86d63b33fdcd40012b0ab000f72c385ca0976a73b3adf95f9653b6c9a963`.
- Full package source/signature/dependency/ZIP-byte audit: `messaging-repair-package-audit.json` in the same directory.
- A local Windows server build is in `Server`; it is not a deployed Pi build and retains the base cache feature gate. Cached-transfer integration uses a separate isolated server compiled with **both** `PEERCHAT_CACHE_CANDIDATE` and `PEERCHAT_DM_CACHE_CANDIDATE`, plus the existing runtime configuration and protected-volume policy. `-DirectCacheCandidate` alone does not enable the base cache gate. Do not deploy the local Windows binary as a Pi package. No server installation, restart, migration or live account change was performed.

## Physical acceptance / deployment boundary

1. Update clients without uninstalling or clearing their data. Confirm ordinary messages, existing verified direct transfers and group voice remain functional.
2. With the current unchanged Pi, expect the new enrollment directory to be unavailable. Do not interpret that as a reason to reset trust, drop a recipient, or repeatedly approve unrelated devices.
3. After separately approved server rollout, choose an authority on each disposable test account, compare its full fingerprint through a trusted channel once, and explicitly endorse the intended additional device. Unknown and old registrations without possession evidence remain unresolved; they are not automatically trusted.
4. Exercise cached files both ways with one authorized recipient temporarily offline. Verify every required recipient's decrypted file and signed receipt; reconnect/restart and confirm that existing authority consent persists without a new prompt.
5. Keep authority replacement/recovery, recursive delegation and unrelated media changes outside this checkpoint. No physical acceptance is claimed by these automated fixtures.
