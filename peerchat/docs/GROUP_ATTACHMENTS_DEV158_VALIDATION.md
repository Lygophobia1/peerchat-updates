# Windows group attachments — dev.158 test candidate

## Scope and checkpoint

This is the focused completion of `3908e76`, resumed from clean `de3b643` on
`codex/native-unicode-emoji`. Worktree root:
`C:/Users/Livel/.codex/worktrees/native-unicode-emoji/Create 2D Game`.
No other branch/worktree was edited, merged, reset or deleted. Accepted dev.148,
dev.153 and earlier packages remain available. No live Pi or phone was changed.

Checkpoints so far:

- `8c24b4b`: Windows drafts, verified actions, older attachment paging and UI tests.
- `02fda44`: integrated application recovery corrections and actual-server tests.
- `cfc3fa2`: explicitly gated dev.158 candidate builds and gate audit.
- Final configuration/documentation hashes are recorded in the package handoff.

The old STOP handoff remains an archival record. This report and the current
integration-manifest addendum supersede its incomplete implementation status.

## Implemented workflow

Windows file selection/drop now creates a bounded draft (up to four regular
files), showing original names/sizes and a bounded image thumbnail when supported.
Send is explicit; selection does not upload. Metadata is rechecked before Send,
and native file handles pin the real input. Clipboard PNG drafts remain bounded.
The accepted text/emoji composer, favorites and multi-insertion are preserved.

The existing Application controller composes the existing TransferManager,
protected ciphertext staging, authenticated DiscoveryClient RPC, immutable cache
descriptor and attachment-only per-device HPKE envelopes. There is no second
transfer/retry engine, historical group-key release or new PCGE message format.

One logical attachment bubble has upload progress and separate per-recipient
delivery/verification evidence. Ciphertext upload completion is never presented
as recipient plaintext verification. Eligible offline recipients independently
accept/download and verify AEAD plus SHA-256. Verified Open/Save remain bounded
worker operations; neither returns an unverified path. Internal output names have
an attachment-ID prefix to avoid collisions; Save uses the original filename and
never overwrites an existing destination. Older attachment history pages are
bounded, profile/group scoped, deduplicated and keep the viewport stable.

## Demonstrated defects and corrections

| Defect | Correction and validation |
| --- | --- |
| Integration fixture enqueued before authenticated capability discovery and expected synchronous Open | Service real `cache_pending_keys`; pump bounded verification until a path is ready. No security bypass. |
| Persisted incoming envelope could be left unacknowledged after restart | Re-ACK the exact retained envelope after authenticated validation. Duplicate replies remain harmless. |
| Releasing a canceled lease could revive the canceled job when the network failed | Cancellation stays terminal; failed cleanup leaves only safe guidance and natural server lease expiry. |
| Restart/rebinding discarded the source's still-owned upload lease | Persist the lease in protected job history and rebind a fresh local route; server ownership/session checks remain authoritative. Expired leases still require normal reacquisition. |
| Crash between immutable staging and application-history commit stranded ciphertext | Recover the exact protected staged manifest/ciphertext into the original job, preserving ID, key/nonce and object binding. |
| Interrupted unsealed preparation had no usable retry and could loop | Terminal preparation failure plus explicit original-file reselection. A fresh key is allowed only before any immutable body, descriptor or admission exists. Sealed retries never re-encrypt. |
| Receipt failure could destroy local verified completion or retry rapidly | Keep verified output; bounded receipt retry, with remote expiry/unavailability reported independently. |
| Old history/open/export work competed with presentation refresh | Bounded native paging and serialized asynchronous UI actions, with group/profile rechecks. |
| .NET Framework server rejected the pinned volume-GUID storage path | Exact probe reproduced `FileStream` / `Illegal characters in path`; modern Framework path switches open the same owned fixture. Ship `PeerChat.Server.exe.config` beside the server, including the Admin copy. No pathname fallback or weakened storage checks. |

Protected `AttachmentJob/v3` adds lease and original input size; v1/v2 remain
readable. Legacy DM journals/message history are unchanged. Preparation leftovers
are cleaned only through authenticated ownership records; unknown files are not
deleted. Prepared-key expiration uses a small worker budget, not another thread.

## Production-path end-to-end evidence

`tests/test-windows-group-attachment-application.ps1` runs real Windows C++
Application + TransferManager + CNG/HPKE/protected key registry, production managed
DiscoveryClient over pinned TLS, and an isolated actual server. Actors have
independent account sessions, identities, storage and key providers. This is not
a mock server, but it is a controlled single-host fixture, not physical PCs/Pi.

The payload is 2 MiB + 1 byte with a Unicode filename. Proven cases:

- One immutable object and one unique ciphertext upload; two independently
  authenticated original recipients download sequentially with sender signed out.
- Source restart during unsealed preparation, protected staging, partial upload
  and envelope publication; exact logical identity retained.
- Recipient restart before envelope ACK, during partial download, during
  finalization, and after local completion before receipt delivery.
- Server restart; exact verified plaintext and safe original-name export;
  duplicate server replies do not create extra jobs/bubbles.
- Both signed plaintext-verification receipts independently verify at the sender
  after its return; key ACKs no longer remain pending.
- Original epoch 5, current epoch 7: valid recipients succeed without either full
  historical group key. Missing authenticated original evidence releases no
  plaintext metadata. Late member and removed/rejoined member are denied.
- Cancel followed by failed release remains canceled; disabled-server capability
  is unavailable rather than falsely supported.

Passing evidence (absolute fixture root `F:/PeerChat-isolated-cache-tests/`):

- `application-561c3d94d6c8482e865e990518db2fe1`: test-host actual server, lifecycle.
- `application-48bf62c2da5a4915a51241e9927242f4`: separate portable candidate server
  process through real entry point, including missing-evidence rejection.
- `application-ca9f1320681843cdbba196fd281d259a`: final Framework dev.158
  executable plus runtime configuration, full sender/two-recipient workflow PASS.

Failed attempts are preserved, not counted as passes: incorrect test-only route
counter access; candidate harness pipe double-disposal; a filename assertion that
incorrectly omitted the established internal collision prefix; and the genuine
Framework storage-path failure above. The corresponding corrections were tested.

## Other validation already completed in this pass

| Test | Result / boundary |
| --- | --- |
| Protected history | PASS 205 lifetime records, 64 active, bounded paging/CAS, no-op persistence, corruption/profile/schema isolation. `windows-group-history-a8799bfb748c4833889474406c2604ec`. |
| Verified output/export | PASS hash/identity tamper, substitution, zero/boundary/Unicode files, no-overwrite and race rejection. `windows-group-output-583283e027074f06a6619fb154ddfc5b`. |
| Durable ciphertext/finalizer | PASS fresh-process partial/full/renamed/orphan recovery and purpose isolation. `windows-group-transfer-cc5ab3ae0de547cca13d98ae9d7015e4`. |
| Cache runtime/security | PASS quota, concurrent reservations, eviction/expiry, protected leases, crash/restart, wrong instance/schema and revocation. `build/tests/cache-runtime-bf71c072f74e4b5c8384220de0fee8b0`. |
| Final dev.158 UI | PASS drafts, paging, dedupe, verified-only actions, stale ABI replies and composer. `build/tests/group-attachment-ui-521c8b37f30b441ba160ef8038f2d581`. |
| Final dev.158 DM attachment UI | PASS exact Unicode/text/preview bounds and long-message scroll. `build/tests/chat-attachments-709a9b90d2ab47269d1cae8c9a3dac79`. |
| TransferManager | PASS ordinary binary, durability, source-change/hash/disk/rename/cancel cases. |
| Negotiated pipeline | PASS 32/64 KiB × 1/4/8/16, stale ACKs/legacy, sender accounting and settings. `build/tests/pipeline-c8ce48a9c03e4412b1ecf77c85ed1add`. |
| History Sync boundary | PASS 25 committed imports, 3 protected unresolved records, five restarts, enrollment/logout/fresh metadata; isolated actual paired workers. |
| Group text / server compatibility | PASS signed encrypted group text, identity/epoch guards, actual TLS mailbox fetch/ACK and server portable/Framework compatibility. |
| Emoji | PASS colored/favorites/multi-insertion/undo/compact draft and encrypted Unicode/restart round trips. `build/tests/emoji-ebbe5f7922fd49baa90bc4a82765cb02`. |
| Media/transfer coexistence | PASS actual TLS control + UDP WebRTC media, transfer resume across media stop/reconnect and journal failure isolation. `build/tests/media-transfer-06c2db1330244ac4adeb1593a07bc157`. |
| Final group media | PASS bounded topology/authorization, one capture/encode with two RTP/MFT viewers, viewer removal, and 30 group audio lifetimes. |
| Build/dependency gates | PASS final Windows build, restricted DLL loading, HPKE notices, native media ABI lifetimes; Linux ARM32/ARM64/x64 version/runtime/architecture audits. |

Some broad regressions used the dev.156 intermediate package before the final
small controller/UI/configuration corrections; affected tests were rerun afterward.
The unchanged media/codec and ordinary protocol code was not repeatedly retested
merely for version changes. Existing managed compiler warnings remain; native
fixture compilation uses warnings as errors. No failing final validation is waived.

The stale DM font assertion was investigated, not suppressed: accepted rendering
already uses `Segoe UI, Segoe UI Emoji` / `Consolas, Segoe UI Emoji`, and the
Unicode-text scroll test must first select the existing text-view toggle.

## Measurement boundaries

No physical throughput improvement is claimed. Controlled host comparisons in
this pass measured default packaged transfers around 5.37–5.50 Mbps, with
diagnostics open around 5.42–5.43 Mbps; a separate negotiated 64 KiB × 8
10-MiB mutual-TLS host fixture measured 71.04 Mbps. These differ in settings and
topology and do not measure Pi/LAN throughput. Cache fanout remains the existing
bounded 32-KiB RPC adapter; ordinary compatible peer negotiation remains intact.
Default 32 KiB × 1 and the 256-MiB ordinary file limit are unchanged.

## Gates, packages and limitations

Only explicit `-CacheCandidate` / `AttachmentCacheCandidate=true` builds include
`PEERCHAT_CACHE_CANDIDATE`. Ordinary builds retain a false transport gate.
Both require administrator configuration `Enabled=true` and valid owned storage;
defaults remain disabled. Metadata audits prove candidate/closed distinctions and
absence of the test fault injector. No test define is used in deliverable servers.

Windows package: `build/development/4.0.2-dev.158/PeerChat-Windows-4.0.2-dev.158.zip`.
Pi ARM32 package: `build/linux/4.0.2-dev.158/PeerChat.Server-4.0.2-dev.158-linux-arm.zip`.
ARM32 is self-contained with pinned .NET 10.0.12; ARM64/x64 packages are
framework-dependent. Hashes/source correspondence are in the package manifests.
See `GROUP_ATTACHMENTS_DEV158_DEPLOYMENT.md` before any installation.

Physical Windows group-attachment and Linux runtime acceptance remain PENDING.
No live deployment/restart was performed. Android cached group attachments are
NOT implemented. Android's separate text/voice branch was inspected at `ade45a4`
(code `227ce31`); its group application owns portable/JNI/session integration.
This milestone adds only Windows-local APIs and protected supplemental records.
No merge is authorized; future Android integration must agree those boundaries.

Historical signed group-device evidence must exist locally and match the server
roster. An unavailable prefix remains a recoverable fail-closed block, not permission
to invent membership or grant older history. Revoked recipients retain any already
completed local copies; eviction only affects future cache downloads.

## Physical test checklist, after separate deployment approval

1. Back up profiles; extract the Windows ZIP to a new directory, preserving the
   current accepted package. Use existing authorized group devices and keys.
2. Approve the documented Pi plan first; do not expect group attachment controls
   to work on an old/gated server or enable storage without verified bindings.
3. Select/drop a picture and binary file: preview/name, explicit Send, one bubble,
   upload progress versus recipient verified status; check emoji/text still work.
4. Download on two Windows recipients, one initially offline, after sender exits.
   Compare original and saved file SHA-256; open image and save exact original name.
5. Interrupt/restart a sender and a recipient mid-transfer; retry and verify no
   duplicate message, corrupt output or reset confirmed prefix. Check cancellation.
6. Switch conversations/restart; load older attachment pages; verify Open/Save and
   per-recipient evidence persist. Spot-check DM file/text, voice and screen sharing.

Stop at this candidate for physical acceptance. No broader roadmap phase follows.
