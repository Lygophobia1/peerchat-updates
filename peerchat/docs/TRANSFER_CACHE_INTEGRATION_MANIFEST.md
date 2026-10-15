# Transfer cache integration manifest

This branch owns the Windows transfer and encrypted attachment-cache milestone.
Attachment-only per-device HPKE envelopes are user-approved. Shipping cache
transport remains disabled; successful component tests do not establish packaged
group delivery or physical acceptance.

## Branches and inspected interfaces

Current branch: `codex/native-unicode-emoji`, worktree
`C:/Users/Livel/.codex/worktrees/native-unicode-emoji/Create 2D Game`.
The shared inspection baseline is `e0940de`.

The inspected parallel task is **PeerChat Android groups and voice**, branch
`codex/android-groups-voice`, worktree
`C:/Users/Livel/Documents/Codex/peerchat-android-groups-voice`. At inspection its
HEAD was `e0940de`; its working changes were Android `MailboxExecution.kt` and
`MailboxIsolationTest.kt`. The new Kotlin `MailboxNative` boundary delegates to
the existing ProfileBridge/MailboxBridge APIs. It separates recipient-registration
failure from outbound/receipt servicing without changing encryption or routing.
Those changes belong to that branch and have not been copied or edited here.

That task plans secure Android group text/application integration followed by
voice. A concrete new shared group/voice C ABI was not yet published. Therefore
this milestone does not define competing JNI, Kotlin, group-session or voice
APIs. Before touching those boundaries, inspect its latest plan and commits again;
do not infer interface agreement from this initial inspection. No cross-task
message, merge or approval by the other task is claimed.

| Boundary | Dependency and integration rule |
| --- | --- |
| `portable/peerchat_core.h`, `portable/peerchat_core.cpp`, JNI bridges | Unchanged here. Android group/voice interface planning must precede any extension. |
| `group.hpp`, `group_key_manager.hpp`, `group_message_envelope.hpp/.cpp` | Unchanged here. Preserve signed ledgers, current-epoch PCGE admission and full-group-key handoff guards. |
| `device_key_manager.hpp`, `mailbox_protocol.hpp`, HPKE/provider interfaces | Unchanged here. Reuse signed recipient bindings and operation leases; never create replacement key custody. |
| Android mailbox application execution | Owned by parallel branch. No duplicate messaging repair here; root cause and physical acceptance remain qualified. |
| Historical/current roster evidence | Attachment decryption requires both verified contexts, exact original/current device-incarnation membership and authenticated cache expiry. A current roster alone cannot manufacture historical entitlement. |
| Windows Node/application transfer owner | Client integration is still pending. Reuse the existing TransferManager; do not create another chunk, journal, ACK or retry engine. |

## Modified shared files and new contracts

The attachment codec is checkpointed at `1bea3e0`; its gated server adapter and
durable-storage/security tests are checkpointed at `21403e4`.

- `group_attachment_descriptor.hpp`: additive `PublicDescriptor` parsing and
  manifest matching. Existing signed upload descriptor bytes stay unchanged.
- New `group_attachment_key_envelope.hpp`: bounded attachment-only HPKE codec.
  `keys::Authority` borrows verified original/current ledgers and roster snapshots,
  authenticated source/recipient membership incarnations and cache expiry.
  It establishes no trust, session, persistence, ACK or history authority.
- `server/AttachmentCacheDescriptor.cs`: source signature verifier for the new
  envelope; existing descriptor serialization remains unchanged.
- `server/AttachmentCacheAdmission.cs`: partial-class split only. New admission
  methods reside in `server/AttachmentCacheKeys.cs` and retain original/current
  group, device-session and recipient-key checks.
- `server/AttachmentCacheRuntime.cs`: initializes/reconciles a supplemental key
  table in the already-owned cache and includes it in cleanup. Existing history,
  account, mailbox and transfer-journal schemas are unchanged by this extension.
- `server/AttachmentCacheRequests.cs`: additive gated cache operations below.
  No discovery, relay, login, presence or normal mailbox protocol changes.

Supporting tests: `group_attachment_key_test.cpp`,
`test-group-attachment-keys.ps1`, `attachment_cache_keys_test.cs`, the existing
`attachment_cache_runtime_test.cs`, and its portable test project. Test-only
factories use isolated databases and generated keys, never production profiles.

## Protocol and storage additions

The attachment-only envelope uses domain `PeerChat/group-attachment-key/v1`,
signature domain `PeerChat/group-attachment-key-signature/v1`, and HPKE info
`PeerChat/group-attachment-key-HPKE/Base/v1`. It reuses the existing P-256 HPKE
suite and signed `device-recipient-binding/v1` bytes. The complete wire is bounded
at 8192 bytes; encrypted private attachment bodies remain bounded at 4096 bytes.

The signed header binds the existing signed upload descriptor, original source
and recipient membership incarnations, exact signed recipient key binding and
creation/expiry times. HPKE AAD binds that header. Only the encrypted body carries
the file key, nonce prefix, filename and plaintext digest. It carries no full
group epoch key. Expiration is bounded by the authenticated configured cache
deadline, not the unrelated ordinary-mailbox TTL.

New RPCs are `cache_recipient_keys`, `cache_publish_key`, `cache_key`,
`cache_key_ack`, and `cache_pending_keys`. They use the existing device-bound
authenticated cache RPC dispatch and remain behind its closed shipping gate.
Group-purpose key lookup does not relax friendship-specific mailbox lookup.
Only original/current eligible devices can receive a key. Missing registered
keys remain explicitly unavailable, not auto-created or auto-trusted.

The supplemental `attachment_key_envelopes` table stores immutable per-object,
per-device ciphertext, digest, key identity/generation and an independent receipt.
Writes are bounded, atomic and quota-checked. This receipt means only envelope
retention; it never advances a transfer offset, authenticates plaintext completion
or marks a message delivered. Retirement allows an exact existing envelope to
remain readable with its retained key; explicit revocation rejects it. Rejoin,
new membership and key substitution do not regain older attachments.

Recipient-binding discovery and envelope RPCs use canonical base64url. A binding
is public signed key metadata, not a registration or group-approval operation.

## Compatibility and package requirements

The next independent Windows adapter is protected preparation of an exact signed
key envelope before its first RPC publication. It will use the existing file and
record-protection adapters, reuse the attachment codec, and own no transfer,
retry, session or group-message history. A profile/server-bound preparation file
will retain identical ciphertext across restart; changed drafts, corrupt storage
or revoked authority must not silently generate a replacement. This adapter
requires no core C export/JNI or Android group interface change. Any later
application/history bridge remains subject to the dependency checks above.

Existing PCGE, DM/mailbox, archive and file-transfer wire formats are unchanged.
No numeric peer-message type, existing C export or JNI signature is changed.
Old clients must never receive a key envelope as ordinary group text. New
attachment operations require a compatible cache server and explicit capability
handling; unsupported servers fail closed rather than falling back to plaintext.
Existing Android group-text and voice work need not consume these APIs to build.

The new header is portable source, but actual Android provider/ABI integration
is not claimed or independently planned here. Later Android attachment support
must use that branch's approved application boundary and existing opaque key
provider. Missing historical evidence remains a recoverable authorization block,
not permission to bypass group-message admission.

Per-device key envelopes differ between recipients. They must NOT be inserted
as if they were the identical PCGE wire in legacy group-message history: existing
message-ID conflict checks intentionally reject different wires. The stable
logical attachment identity is the signed descriptor's group/message/object
binding. The eventual application owner must keep that canonical identity,
protected private metadata and per-recipient transport evidence distinct. Agree
its group UI/history projection with the parallel group-text interface before
adding shared application APIs; do not bypass deduplication or archive validation.

The parallel task attempted Android package number dev.156. Do not automatically
reuse that number for a different unified Windows/cache candidate. Allocate and
record distinct package versions when each branch is ready; preserve the dev.153
accepted packages and dev.155 diagnostic APK. No final cache package version is
allocated yet, and no live Pi installation is authorized by this manifest.

## Validated checkpoints and evidence

- `dc97469`: Android dev.155 diagnostics, not a proven messaging repair.
- `0607d4a`: dev.154 negotiated transfer pipeline, default 32 KiB by 1; physical
  acceptance of the new tuning remains separate.
- `ffc7d4d`: durable signed announcements and independent receipts.
- `10be929`: ownership check before cache schema changes.
- `446ab48`: bounded encryption preparation before protected key handoff.
- `09edaaf`: protected ciphertext recovery across process death and rename.
- `e0940de`: reproduced missing-key/stale-epoch rejection and protocol-review
  boundary; common baseline of both active worktrees.
- `72f9cc7`: initial parallel-interface inventory and integration boundary.
- `1bea3e0`: attachment-only native HPKE codec and actual provider/registry tests.
- `21403e4`: original/current group admission, immutable key-envelope persistence,
  quota/revocation/restart tests and pinned-TLS cache RPC integration.

Current evidence includes actual shipping HPKE/CNG and protected-registry tests, native to managed
durable-cache to native decryption, authoritative key lookup/publication,
rotation/revocation/rejoin, independent receipts, restart and corruption rejection.
The full isolated cache quota/ownership/crash suite passes with the new table.
Pinned-TLS fixture operations publish keys, log the source out and allow an
independently authenticated original recipient to retrieve/ACK them. Its managed
server factory uses opaque signed payloads; actual HPKE is proved by the separate
native round trip, not fabricated from that server fixture.

Additional validation passed 16 concurrent metadata admissions under a reduced
quota, clean metadata rejection preserving completed ciphertext, expired-key
polling alongside valid envelopes, the existing server compatibility suite and
portable group-body/current-epoch regressions. ARM32 Linux compilation passed;
execution on a Pi is not claimed. An ARM32 assembly cannot be loaded into the
Windows test process; the shipping-gate check instead passed against a separately
built host-compatible shipping assembly, with no cache database created despite
an enabled configuration.

Component checkpoint hashes and any later interface dependencies must be added
before a package is represented as integration-ready. Production client dispatch,
durable transfer resume, receiver finalization and group attachment UI are still
pending. Cache enablement, physical throughput and end-user delivery are not
accepted by these component tests.
