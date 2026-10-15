# Group History Sync integration contracts

## Baseline and interface review

The accepted unified baseline is `42da2cfc31d7b2fccd46c07998cb375877695927`, dev.162, on `codex/native-unicode-emoji`. Backup tag: `codex/backup/unified-accepted-dev162-20261015-42da2cf`. The validated membership-evidence foundation is `bd04009`; subsequent integration is not yet a completed or packaged milestone.

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

No reference implementation or acceptance is claimed yet. If the safe typed bridge cannot satisfy the required private-metadata behavior without a new security-policy decision, stop at that boundary instead of weakening the existing contracts.

## Changed shared surfaces and validation status

Text integration currently touches `archive_group_history.hpp`, archive history/sync/execution helpers, `protocol.hpp`, Storage, Node, portable group application/endpoint/core, WPF group paging and Android group projection. Existing P-256/AES providers, PCGE bytes, TLS verification, HPKE envelopes and server implementation remain unchanged.

Passing evidence includes existing real peer-TLS DM archive regression; the new group fixture using Windows and portable crypto; Android ARM64/JNI builds, 223 unit tests and lint; and real Windows Node/Android native-controller TLS recovery of seven missed Windows and five missed Android messages without live fanout. That integration also stops the original Windows source, recovers the second Android adapter's missed records from the remaining member, reopens Windows protected history, and checks removal/key rotation. This is host-side integration, not physical Android acceptance.

Ordering tests cover 75 authenticated records arriving in reverse order, bounded recovery of missing supplemental ordering indexes, opaque cursors, and duplicate-free older paging. Four Kotlin projection tests cover revision-driven refresh without a live event, pending index preparation, profile reset and the opaque older-page boundary. The Windows all-component validation build and restricted-load/runtime dependency audits passed. Android `testDebugUnitTest lintDebug assembleDebug` passed; lint reports existing warnings, not errors.

Evidence remains under ignored `build/`: `group-history-ordering-legacy.log`, `group-history-portable-crypto.log`, `group-history-fallback-interop-2.log`, `group-history-archive-regressions-2.log`, `group-history-attachment-nonregression.log`, `group-history-windows-build.log`, and `group-history-android-projection.log`. The existing native Android group, group relay admission, scoped enrollment and offline mailbox fixtures passed. No real profiles, mailbox queues or server databases were used by those fixtures.

## Remaining boundary and resume steps

Validated logical checkpoints on the existing branch are `bd04009` (epoch membership evidence), `11f4d57` (bounded authenticated text-history integration), and `d05d361` (ordered Windows/Android UI projection). These are partial implementation checkpoints, not a completed release.

Additional nonregressions passed against the isolated current Windows build: automatic verified download saving/restart/collision/overwrite recovery; group attachment bubble/actions and bounded projection; clipboard PNG/DIB/text precedence; emoji color/cache/favorites/multi-insertion/editing/persistence plus encrypted Unicode round trips; native TransferManager and durable transfer restart; real native file-session reconnect/final hash/message ACK coexistence; group voice/screen lifetime/authorization and voice receive-engine behavior. Initial wrapper invocations omitted required application-directory arguments or contained a misplaced shell redirection; those are test-launch errors, not product failures, and the affected invocations were corrected.

The final transfer-pipeline regression also passed: all 32/64 KiB × 1/4/8/16 durable-credit combinations, legacy fallback, timing/duplicate/route checks, bounded caps, tuning UI, managed scheduling and verified packaged-host transfer. The packaged-host fixture has no sockets or Pi; its throughput is not a physical network claim. Evidence: `build/tests/pipeline-e56c851cc1e2485cbdad775a0ba51c84`.

The complete milestone is **not ready for physical acceptance or release packaging**. Approval was requested for the typed public-descriptor attachment-reference lane after the safety review blocked the earlier proposal; no consent is inferred from silence. No rejected implementation was applied. Existing accepted attachment transfers remain unchanged and their application/cache regression passes, but preservation of those references through the new historical lane is not implemented yet.

After that boundary is resolved, implement only the approved reference contract, extend security/interoperability tests, and finish group-role progress reporting. Current same-account scan message counts include group records, but completed-conversation/source counters are not yet comprehensive for group-control recovery; do not interpret them as authoritative group-import totals. Group records requiring unavailable historical epoch evidence or keys remain unresolved. This change does not migrate historical provenance or enable previously gated same-account group enrollment.

Then run affected tests, allocate the next development version, checkpoint, build both packages from that same source checkpoint, audit Android identity/signing/upgrade compatibility, and record hashes. The current validation outputs still identify dev.162 and are intentionally not offered as a new installable milestone. The physically accepted dev.162 package and backup tag are preserved. No completed milestone, new release package, Pi deployment or phone installation is claimed.
