# Android conversation ordering correction — dev.124

Follows the completed media scheduling checkpoint 010ce8a (dev.123), preserving
dev.121 transfer scheduling and accepted dev.119 Android background transfers.
User symptom: incoming live text appears above pictures and a reply can appear
above the outgoing text it answers.

Two presentation defects were demonstrated in production code:

1. chronologicalMessages sorted by sender wall-clock timestamp first, using the
   native logical order only to break ties. Clock differences can place a reply
   ahead of its causal predecessor. Windows already uses logical order, sender
   identity and logical message ID. Android now follows that same deterministic
   order; native sequence remains the pagination/notification/deduplication
   cursor, not a rewritten wire order. Newest durable duplicate projection still
   wins; real ACK/Stored/Delivered states are unchanged.
2. ConversationScreen emitted every text bubble first, then every attachment
   card. A new reply consequently always appeared above the existing pictures,
   regardless of timing. One immutable mixed timeline now feeds the virtualized
   LazyColumn, date headings, sender grouping and scroll-to-bottom calculation.
   Text retains native logical order. Files insert by durable native transfer
   creation time, matching Windows' existing attachment presentation rule.

The current file protocol/journal has no common text/attachment Lamport field.
This fix does not invent one or silently mutate protocol/storage. Cross-device
wall-clock disagreement can still make the relative file/text placement
ambiguous; the text causal order is never sacrificed to sort all rows by time.
Transfer progress/completion time is NOT an ordering input. Exact text, IDs,
timestamps, sender/recipient identities, journal entries and protected files are
unchanged. No second outbox, transfer owner, ACK or retry owner introduced.

Tests exercise clock-skewed outgoing/reply snapshots, reversed arrival and
duplicate ACK projections, exact field preservation, live picture/file insertion,
completion without movement, reconstruction from durable snapshot fields, and
the real Compose conversation while open. Namespaced row keys prevent text/file
ID collisions. Load-older uses the existing global native row cursor; merging
uses the same logical ordering function. Notification read/notified cursors
remain native sequence-based. No identities, histories or grants were modified.

Focused NativeProjectionTest and ConversationTimelineTest passed, including
actual Compose row positions before/after a live update. All 139 Android JVM
tests passed with zero failures/errors; debug/test/release builds, lint and
ARM64/JNI/dependency audits passed. Shared reliable messaging, identity,
ACK/dedupe, Unicode/history, portable Storage/Profile restart/tamper/provider
and group-envelope security regressions passed. The full Windows build and
package/import audit passed, as did unified version consistency and the
dev.123-to-dev.124 development updater manifest/hash/extraction validation.
The previously recorded unrelated Windows long-message layout performance
failure is not claimed resolved by this presentation-ordering correction.

Development-only artifacts under build/development/4.0.2-dev.124:

- Android/PeerChat-4.0.2-dev.124-debug.apk SHA-256:
  3fcb4c6cd0e88f7417e462b8307616afb0446c0c84ba0ddee28cc455b7c43796
- Android/PeerChat-4.0.2-dev.124-release-unsigned.apk SHA-256:
  3e5c201315e4208477f548f369901b51e919ff2ce59fe3ce8692ce278a67b58f
- Android/PeerChat-4.0.2-dev.124-test.apk SHA-256:
  dc4144fd96a06c3d101bc209911591b892240e0fcfdf75d9e4abcaf1132e28a1
- DevelopmentUpdaterValidated/PeerChat-4.0.2-dev.124.zip SHA-256:
  15991120f224a73658a00cfbc7e1015999cc0a8906df814f706c70c915d8d8a9

Physical ordering acceptance is pending: no ADB access, install, new device
test request, deployment, publication or merge. Existing identities, history,
grants and unrelated object files were preserved.
