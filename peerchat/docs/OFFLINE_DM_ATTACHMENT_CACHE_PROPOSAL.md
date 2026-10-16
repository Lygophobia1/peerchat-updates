# Offline direct message attachment cache proposal

## Decision required

Extend the existing encrypted Pi cache with a separately authenticated direct-message scope. This proposal is design-only. It does not enable a new capability, change the server or Android protocol, migrate storage, or deploy anything.

The recommended disconnect boundary is **ciphertext committed and every intended device's private attachment notice durably stored**. A committed blob alone cannot guarantee that an offline recipient will learn about it. The UI must distinguish Upload complete, Notice stored, Downloaded and Verified saved; none implies the next.

## Existing components and missing contracts

The current group cache uses `server/AttachmentCacheDescriptor.cs`, `server/AttachmentCacheAdmission.cs`, `group_attachment_blob.hpp` and `group_attachment_key_envelope.hpp`. Their group ID, epoch, roster and historical-membership checks are genuine authorization requirements. A DM must not supply a dummy group or reuse these permissions.

`mailbox_protocol.hpp`, `server/MailboxProtocol.cs` and `delivery_manager.hpp` already support authenticated per-device envelopes, independent receipts and durable text delivery. The current mailbox body is text-only. Sending an attachment body under that text type would be incompatible. Android has the shared TransferManager and secure file handling, but needs a narrow production cache adapter; Windows controller code cannot simply be copied across filesystem and JNI boundaries.

## Proposed authorization and encryption

1. Negotiate a distinct DM attachment capability and versioned cache RPC scope. Preserve legacy group cache messages and ordinary text unchanged. Unsupported clients report unavailable rather than parsing an attachment as text.
2. Freeze the eligible recipient-device audience before upload, using existing authenticated ownership, consent and verified HPKE public keys. Bind each exact device and key generation. Online presence is not eligibility. Missing verified keys are a visible preflight failure, never an invitation to enroll or trust automatically.
3. Sign a canonical DM public descriptor with a new domain tag. It contains object and logical message IDs, sender account/device/identity, recipient account, relationship authorization incarnation, frozen audience digest, codec version, encrypted size and ciphertext digest. It contains no filename, plaintext digest, MIME type, key or private envelope.
4. Encrypt the file once locally using the existing block encryption implementation with DM-specific domain separation and associated data. Do not create a second transfer engine or reuse group AAD. Put the private filename, MIME/type, plaintext size/hash, blob key/nonce, descriptor and audience binding inside each device's existing HPKE envelope under a separately typed attachment body.
5. Bind the private body to its outer logical message ID, sender, recipient account/device and key generation. Verify the sender signature, authenticated device mapping and current relationship before accepting it. Stable message IDs deduplicate mailbox and authorized History Sync without merging live ACK ownership.
6. Authorize each cache operation from the stored typed scope and authenticated current device session. Recheck friendship/block state, device ownership, revocation and the original audience on reserve, upload, status and download. Unfriend/re-add and device re-enrollment require a distinct authorization incarnation. A short-lived eligibility snapshot is not a 48-hour download grant.

No later device enrollment, key rotation or new friendship silently expands access to old attachments. Previously downloaded plaintext cannot be remotely revoked. Sender-companion mirroring is not included in this minimal proposal.

## Durability and delivery

Reuse existing cache reservations, upload offsets, protected staging, TransferManager journal, download resume and final integrity verification. A narrow typed transfer-purpose adapter must preserve existing serialized purpose values and direct-transfer behavior.

Persist the sender's exact signed descriptor, frozen audience, source token and server/profile/device binding with the existing protected job state. Retry uploads and per-device notice publication idempotently. Do not rebuild envelopes or redirect a queued notice to a different device. Confirm the disconnect boundary only after both durable cache commit and durable notice publication for all frozen recipients.

Mailbox receipt acknowledges the encrypted notice, not the attachment bytes. Track each recipient's authenticated verified-file receipt separately, only after decryption, full integrity verification and durable finalization. Persist exact receipt bytes and job progress atomically so restart cannot fabricate completion or inflate counters. The download UI retains verified internal files if user export fails.

The Android adapter must distinguish the ciphertext container bound from the unchanged 256 MiB plaintext file limit. Encryption overhead is not permission to raise the ordinary file limit.

## Storage and expiry

Use the existing configurable cache directory, 10 GiB default quota, 48-hour default maximum retention and actual-filesystem reserve policy. Include partials, metadata, temporary files and concurrent reservations. Preserve active protected leases and fail safely on unavailable storage. Expired objects are removed first; quota pressure may evict the oldest eligible completed object early. Thus 48 hours is a maximum, not guaranteed availability. Expose an explicit expired/evicted/unavailable result without deleting recipients' completed copies.

## Security review and implementation gates

The independent review found this feasible only with a new typed DM authorization contract, typed private mailbox body, domain-separated blob context and Android cache adapter. Group permissions, a bearer descriptor, or the existing brief eligibility snapshot alone are insufficient.

Approval is required before implementing those additive protocol changes, any persistence migration or Pi deployment. Proposed product decisions are: freeze recipient devices at send time; require all private notices stored before disconnect; retain best-effort expiry with early quota eviction; exclude sender-companion mirroring; recheck relationship and revocation on access.

## Required validation after approval

- Production Windows to Android and reverse uploads while recipients are offline; sender restart after each durability boundary.
- Two recipient devices with independent download offsets, receipt state and duplicate suppression; one device consuming a notice cannot consume another's.
- Interrupted uploads/downloads, reconnect, replay, duplicate completion and crash recovery without losing the verified internal copy.
- Forged descriptors, cross-account/device/scope substitution, stale keys, revoked devices, blocked/unfriended relationships and re-enrollment.
- No keys, plaintext metadata or message bodies on the Pi or in diagnostics.
- Expiration, early eviction, concurrent quota reservations, disk disappearance and protected leases.
- Legacy DM text/direct attachments, group attachments and History Sync regression coverage; no false live ACK from a history import.
