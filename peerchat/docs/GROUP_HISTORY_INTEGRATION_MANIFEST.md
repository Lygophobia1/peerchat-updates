# Group History Sync integration contracts

## Baseline and interface review

The accepted unified baseline is `42da2cfc31d7b2fccd46c07998cb375877695927`, dev.162, on `codex/native-unicode-emoji`. Backup tag: `codex/backup/unified-accepted-dev162-20261015-42da2cf`. Membership evidence is checkpointed at `bd04009`; text/UI integration at `11f4d57` and `d05d361`; approved public references at `46e2d2d`. Matched dev.163 Windows and Android test packages were built from `1ebc47ae16dc5eb96728d2d80da448ed36e43301`. Physical acceptance remains pending.

The existing Android branch remains at `ac7ab34083b84276ca97b10f19ceeaaa58241896`, clean and unchanged. Its planned/shared contracts were reviewed in `ANDROID_GROUPS_VOICE_INTEGRATION.md`, including the native group controller, scoped TLS admission, bounded GroupBridge operations and disabled media gate. Main's `TRANSFER_CACHE_INTEGRATION_MANIFEST.md`, `GROUP_ATTACHMENT_CACHE_DESIGN.md`, current HPKE codec, public descriptor and Windows attachment controller were reviewed before planning the attachment-history boundary.

## Existing contracts to preserve

- PCGE v1 is the existing shared encrypted, signed group-message envelope. Live admission remains current-epoch only. Historical import requires independently retained protected epoch evidence, matching signed membership instances, current membership and the appropriate retained epoch key. Current membership alone does not authorize earlier epochs.
- Archive grants authorize exact same-account source/reader devices under the same trusted issuer. Cross-account group recovery instead uses the already admitted group-scoped peer-TLS route, with exact current group/device/generation admission and negotiated history capability. It never creates friendship, DM authority or an archive grant.
- `GroupBridge.request` remains the existing JNI boundary. Group operations 2 and 3 gain optional, versioned local projection trailers; no JNI signature or portable C ABI change is needed. Old local projection fixtures remain readable.
- Existing group-control transport and bounded SessionTransportManager demand/retry/winner ownership remain authoritative. The history lane introduces no socket, thread, transfer manager or account session owner.
- The encrypted attachment cache retains its existing immutable public descriptor, per-device HPKE envelopes, original/current membership and device-incarnation checks, expiration and independent receipts. No cache RPC, transfer window, ciphertext, receipt, key-provisioning or Pi configuration change is part of group history.
- Attachment key packets differ by recipient. They must not be inserted into the identical-wire PCGE history/deduplication store. Private filenames, nonce prefixes, content keys and plaintext hashes remain protected or inside the existing authorized encrypted attachment body.

## Text history integration under validation

Capability `16384` negotiates `PeerChat/ArchiveGroupHistory/v1` inside an admitted archive or group-control TLS purpose. Pages are bounded by the existing 70,000-byte peer frame ceiling and eight records. Requests bind fresh identifiers, source store identity and cursor windows. Source filtering and receiver verification both enforce exact scope. Completion acknowledgment for group-control recovery follows durable verified imports/cursor persistence, not transport connection or response submission.

`GroupMember.membershipId` is derived from existing admitted signed create/accept/device-approval events. It is not a new wire field. Rejoining with the same key does not revive previous historical authority. Protected epoch evidence is captured only from the existing authenticated roster matched to the signed ledger, never synthesized from an old message or a current naked roster. No old epoch keys are released by history.

The original group-message index remains intact. An additive ordering index stores only authenticated order/public sender metadata, preserving protected message content and legacy access. Backfill is bounded; page cursors follow logical order rather than late import arrival. WPF refreshes are coalesced on the existing worker; Android uses the native history revision with bounded reconciliation.

## Attachment reference review boundary

A proposed new signed filename/history-reference codec was rejected by the safety review and was not applied. That approach would also risk mixing non-PCGE records into a store whose accepted readers assume PCGE, and could broaden private metadata exposure beyond per-device HPKE admission. It is not the integration plan.

The safer alternative is to reuse the **unchanged existing signed public upload descriptor** as the reference evidence. It already binds the original group/message/object identity, sender device/account, epoch, ciphertext digest and lengths. It does not authenticate a private filename, original ordering timestamp or attachment decryption key; those must not be fabricated or copied into public reference fields.

Any implemented reference lane must therefore:

1. Keep references in separately typed, protected supplemental records/indexes in the existing profile store, not the legacy PCGE message table and not another messaging database.
2. Reuse the existing descriptor bytes and signature. Add no general signing operation, new identity/key, replacement attachment envelope or server protocol.
3. Apply original/current exact device membership and revocation checks before serving or importing reference evidence. Missing historical evidence remains unresolved.
4. Project an explicitly identified encrypted-attachment reference with unavailable private fields shown honestly. Preserve existing fully authorized local attachment bubbles/actions where present; never show a duplicate logical attachment.
5. Leave filename/private metadata and actual downloads to the existing per-device HPKE/cache controller. A reference is not authorization to decrypt, a completed download or proof that expired cache ciphertext still exists.
6. Use a negotiated typed extension of the bounded history lane and an optional local JNI/UI projection, with legacy PCGE/DM readers unchanged. Record exact dependencies and compatibility tests before enabling it.

The user explicitly approved this narrower public-descriptor lane. It is implemented as `PeerChat/ArchiveGroupReferences/v1`, separately typed from PCGE/text records and negotiated with the unpublished group-history capability `16384`. It reuses the same bounded request/page reader, protected CAS cursor, TLS owner and durable completion ACK as text history. Existing released dev.162 clients do not advertise this capability; both test clients need the updated build for group recovery. There is no Pi protocol or C/JNI signature change.

`group_reference_index_v1` is an additive identifier-only index in the existing profile database; immutable descriptor bytes use its existing protected object store. A reference never enters `authenticated-group-message` or the PCGE index. Windows reads existing attachment jobs in bounded pages to extract only their unchanged signed public descriptor; private bodies and transfer ownership stay with the existing attachment controller. Original and current membership-instance checks apply at source, import and projection. References do not release keys or authorize cache operations.

The original descriptor signature covers both public plaintext and ciphertext lengths; preserving its original bytes is necessary for verification. It contains no filename, time or logical message order. Reference-only cards explicitly mark those private/absent fields unavailable rather than inventing them, and do not offer download actions. The original PCGE text order remains unchanged. Windows upgrades the same stable message bubble when independently authorized private attachment metadata exists. Android shows a distinct reference description and excludes it from text pagination and send-retry actions. No expired ciphertext recovery is implied.

## Changed shared surfaces and validation status

Text integration currently touches `archive_group_history.hpp`, archive history/sync/execution helpers, `protocol.hpp`, Storage, Node, portable group application/endpoint/core, WPF group paging and Android group projection. Existing P-256/AES providers, PCGE bytes, TLS verification, HPKE envelopes and server implementation remain unchanged.

Passing evidence includes existing real peer-TLS DM archive regression; the new group fixture using Windows and portable crypto; Android ARM64/JNI builds, 223 unit tests and lint; and real Windows Node/Android native-controller TLS recovery of seven missed Windows and five missed Android messages without live fanout. That integration also stops the original Windows source, recovers the second Android adapter's missed records from the remaining member, reopens Windows protected history, and checks removal/key rotation. This is host-side integration, not physical Android acceptance.

Ordering tests cover 75 authenticated records arriving in reverse order, bounded recovery of missing supplemental ordering indexes, opaque cursors, and duplicate-free older paging. Four Kotlin projection tests cover revision-driven refresh without a live event, pending index preparation, profile reset and the opaque older-page boundary. The Windows all-component validation build and restricted-load/runtime dependency audits passed. Android `testDebugUnitTest lintDebug assembleDebug` passed; lint reports existing warnings, not errors.

Evidence remains under ignored `build/`: `group-history-ordering-legacy.log`, `group-history-portable-crypto.log`, `group-history-fallback-interop-2.log`, `group-history-archive-regressions-2.log`, `group-history-attachment-nonregression.log`, `group-history-windows-build.log`, and `group-history-android-projection.log`. The existing native Android group, group relay admission, scoped enrollment and offline mailbox fixtures passed. No real profiles, mailbox queues or server databases were used by those fixtures.

## Remaining boundary and resume steps

Validated logical checkpoints on the existing branch are `bd04009` (epoch membership evidence), `11f4d57` (bounded authenticated text-history integration), and `d05d361` (ordered Windows/Android UI projection). These are partial implementation checkpoints, not a completed release.

Additional nonregressions passed against the isolated current Windows build: automatic verified download saving/restart/collision/overwrite recovery; group attachment bubble/actions and bounded projection; clipboard PNG/DIB/text precedence; emoji color/cache/favorites/multi-insertion/editing/persistence plus encrypted Unicode round trips; native TransferManager and durable transfer restart; real native file-session reconnect/final hash/message ACK coexistence; group voice/screen lifetime/authorization and voice receive-engine behavior. Initial wrapper invocations omitted required application-directory arguments or contained a misplaced shell redirection; those are test-launch errors, not product failures, and the affected invocations were corrected.

The final transfer-pipeline regression also passed: all 32/64 KiB × 1/4/8/16 durable-credit combinations, legacy fallback, timing/duplicate/route checks, bounded caps, tuning UI, managed scheduling and verified packaged-host transfer. The packaged-host fixture has no sockets or Pi; its throughput is not a physical network claim. Evidence: `build/tests/pipeline-e56c851cc1e2485cbdad775a0ba51c84`.

Reference security and bounded paging tests pass with both Windows and portable crypto: 17 signed descriptors, three pages, no private filename/key release, account/group substitution rejection, signature tamper, unrequested replay, missing keys, newly admitted/revoked devices, protected restart and deduplication. The real Windows Node/Android native-controller TLS fixture imports references in both directions and through an alternate member with the original Windows source stopped. Its first reference assertion ran at text-only completion; the corrected fixture waits for the actual reference lane before teardown, retaining the same security checks. Android now passes 225 unit tests plus lint and ARM64/JNI build. The WPF reference-to-authorized-attachment upgrade regression passes.

Same-account archive record counters include text and references; completed group counts are deduplicated across both lanes and counted only after an unresolved-free scan. Source queued counters include eligible group response records and retries, not delivery proof. Group-control reconciliation is separate from that same-account archive status; its imported rows and durable native revision drive conversation refresh. Per-route text/reference reconciliation is not represented as a fabricated global percentage. Group records requiring unavailable historical epoch evidence or keys remain unresolved. This change does not migrate historical provenance or enable previously gated same-account group enrollment.

Final dev.163 builds, audits and package regressions passed. Explicit restart between reference pages also passed against unchanged production sources. Package paths, SHA-256 hashes, Android signing/upgrade checks, test evidence and the physical checklist are in `GROUP_HISTORY_DEV163_TEST_REPORT.md`. Validation outputs identifying dev.162 are not new installable milestone packages. The accepted dev.162 ZIP hash remains unchanged. No Pi deployment, live database mutation, phone installation or physical acceptance was performed.
