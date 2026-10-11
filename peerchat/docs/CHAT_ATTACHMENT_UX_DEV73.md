# Chat attachment UX — 4.0.2-dev.73

This Windows-first milestone projects existing TransferManager snapshots and the
durable attachment catalog into the open direct conversation. It does not add a
transfer, delivery, reconnect, storage, or archive authorization owner.

## Implemented

- Paperclip selects files for the current verified direct conversation; no second
  recipient selection. Up to four files per selection/drop, subject to existing
  transfer limits (256 MiB/file, four active operations, 64 journal records).
- Explicit conversation drop target rejects absent/changed targets, groups,
  directories, and invalid paths. Empty conversations are also drop targets.
- In-chat cards show native preparing, waiting, incoming, sending/receiving,
  verified complete, canceled, failed, revalidating, and paused states. Download
  and Cancel use existing operations. File Transfers remains a secondary view.
- Completed PNG/JPEG content is recognized by signature, not extension. Async
  read-only thumbnails and image viewer are bounded to 320 x 240; source metadata
  over 16 Mi pixels or 16,384 in either dimension is rejected. Decode errors do
  not fail transfers. No automatic executable/script/HTML opening.
- Strict UTF-8 previews accept optional BOM and reject binary/control/NUL or
  malformed content. Monospace previews are limited to 32 KiB / 200 lines; Copy,
  Expand, read-only full text (ordinary files <=1 MiB), and explicit Save As are
  available. Original history attachments are never edited.
- Preview reads have two concurrent slots; cache and resident-card previews each
  retain at most 24 entries. Busy work is explicitly retryable, not unbounded.
- Composer no longer truncates at its old UI maximum. Above 4,096 UTF-16 units it
  offers exact UTF-8 message.txt using the existing reliable transfer. Generated
  text is limited to 512 KiB UTF-8 (and 1 Mi UTF-16 units); oversized, malformed,
  canceled, unverified, or failed enqueue retains the draft.
- Generated snippet sources are DPAPI-protected under the existing protected
  record provider, context-bound to the source UUID. There is no plaintext source
  temp file or new key. Restart/archive source reopen uses the same Source
  interface. Preview decrypts bounded content in memory; only explicit user Save
  As creates a plaintext copy. Ciphertext paths are not exposed as preview paths.
- Transfer timestamps and local read-only restore-to-original mapping preserve
  logical attachment identity, original direction, and authenticated sender.
  File and ordinary message dedupe/ACK handling remain type-distinct.
- Completed cards reload from existing durable catalog even without a live
  operation. Existing journal retention (seven days / 64 records) still governs
  canceled/failed/paused operations. No UI database was introduced.
- Local file/preview command failures return to their caller without becoming a
  misleading global connection error or retiring the reliable session.

## Boundaries and deferred work

Android has matching versioned builds and backend regression coverage, not new
attachment picker/cards/download/preview parity. Group attachment fanout is not
production-ready and is explicitly unavailable; no group transport was added.
GIF/WebP, syntax highlighting, and full-resolution image viewing are deferred.
The image viewer intentionally displays the bounded decoded thumbnail.
Transfers require the existing verified peer route; this milestone does not add
server file storage. Files selected from disk remain user-owned source files.

Archive metadata does not prove source availability. Restoration remains the
existing explicit authorized History/Archive Settings flow, with the resulting
transfer projected into the original attachment bubble. No archive grants,
enrollment, provenance, cursors, wire formats, or server behavior changed.

## Validation

Focused automated coverage includes transfer state/card projection, typed ID
dedupe, restored logical IDs, drop identity, PNG/JPEG wrong extensions, oversized
and corrupt image fallback, strict UTF-8/binary/malformed text, bounded large logs,
Unicode/CRLF exact snippets, UTF-8 boundary trimming, protected-source tamper and
context rejection, no plaintext source leakage, explicit export, and restart.

Real native mutual peer-TLS/opaque-relay tests cover binary and generated text
transfer, hash validation, partial/reconnect/profile reopen, completed sender and
receiver reopen, ordinary message ACK coexistence, and local preview failure
without route retirement. Existing transfer/restart and archive attachment tests
remain applicable. One test fixture initially used a Storage ID helper absent
from that suite's link set; deterministic test-only IDs corrected the fixture.

Regression gates include portable Profile/Storage/opaque identity, DPAPI,
history/outbox/ACK/dedupe, archive authorization/provenance/session scope,
reliable endpoint/protocol lifecycle, presence/offline composer, mailbox protocol
and lifetime, voice settings and topology, packaged WPF and archive application
boundary, full Windows build/import/package/version/updater checks, Android JVM
tests, debug/test/release builds, lint, and JNI/ARM64/dependency audits.
Automated results do not substitute for physical acceptance. All listed gates
passed for this milestone; final packaged chat/transfer/archive UI and actual
archive Node/application tests passed against dev.73. Android JVM tests passed
114 tests with zero failures/errors; debug/test/release builds, lint, and ARM64
audits passed. Lint retains existing warnings, not new attachment UI warnings.
git diff --check passed. Development updater extraction/hash/version consistency
passed from dev.67 to dev.73; this is unsigned Development policy only, not a
release signature or publication.

Final Development ZIP SHA-256:
29840E6727180B9F87807856403DB995361011D5D25AD17CE7E1CE6AF04D91B8.
Final matching debug APK SHA-256:
B86DCA22978A45D834F8D5AE7C541610B377ECAA4B37B35472BACA99023E3F69.

## Artifacts and accepted state

Final candidate: build/development/4.0.2-dev.73/Client/PeerChat.Desktop.exe.
Matching Android artifact: build/development/4.0.2-dev.73/Android/
PeerChat-4.0.2-dev.73-debug.apk. Intermediate dev.68–72 packages are not the final
candidate and must not be used to validate the protected snippet implementation.
Packages remain immutable. No publish, merge, Pi deployment, or identity changes.

History dev.67 / 9a92024dcc69a039717496ab1642bc869089c18b remains frozen and
pending its two-device physical test. Preserved debug APK SHA-256:
CFBFC7BAC34DBB6D38B6881C3458A0B63485CD5CAC05CAEEC2BBB33CAE0269F8.
Preserved updater ZIP SHA-256:
8E2D08126EC81D040B6B4323DDACC51D2C140CD99E5383911C363C01144DEE3A.
Mailbox/background dev.65 / d6c7153 remains physically accepted; no new physical
claim is made by this milestone. No phone was detected during final validation.

## Physical checklist

1. Run two Windows dev.73 clients with existing verified profiles. Paperclip-send
   a PNG and a code file; verify incoming card, Download, progress, verified
   completion, thumbnail, read-only preview, Copy, and Save As.
2. Drop one and multiple files; change conversations during dragging and verify
   no wrong-recipient send. Try an empty conversation, no target, and a group.
3. Cancel an incoming/active transfer; exercise a failed/unavailable file and
   confirm truthful state without losing ordinary message connectivity.
4. Paste multiline Unicode text above 4,096 units. Confirm conversion, exact text
   on receiver, full read-only view/export, and retained draft on cancel/failure.
5. Restart during an accepted partial transfer and after completion; verify
   existing resume, stable single bubbles, local files, and protected snippets.
6. Recheck ordinary messaging/mailbox fallback and call/share lifecycle. History
   dev.67 physical acceptance remains a separate pending test.
