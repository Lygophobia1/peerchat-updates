# Android long messages and bounded text previews — dev.108

Builds on dev.107 pictures/files, without changing SessionTransportManager,
DeliveryManager, TransferManager wire frames, archive authorization or identities.
Additional-device group enrollment remains disabled at the verified dev.106 root.

Typed text beyond 4,096 UTF-16 characters is offered as the existing Windows
`message.peerchat-text` representation, bounded to 512 KiB of exact UTF-8. The
native source adapter protects its local backing record through the existing
Android protected-record provider. TransferManager receives the original bytes,
owns SHA-256/chunking/durable restart and completion acknowledgements. Source
tokens are local-only, purpose/transfer-ID bound, and cannot replace a private key.
Receiving still uses the existing explicit transfer acceptance. No file mailbox
or automatic download policy is introduced.

The chat card says Long message, not a filename. Prose uses normal typography;
code-like text and text/code files use monospace. A virtualized, bounded-height
viewer provides scrolling, Expand, Copy and read-only Open full. Copy retains the
original string and line breaks, independent of bounded layout chunks. Preview
results/errors are account/generation guarded and the UI cache holds at most four
previews. Actual files remain attachments. Oversized unsent drafts stay in memory;
accepted sources and transfer state are durable. The existing 64-record transfer
journal projection still needs catalog-backed older attachment pagination.

## Automated validation

- 127 Android JVM/Compose tests, zero failures; debug/test/unsigned release builds
  and lint pass. Unicode/surrogate boundaries, multiline/prose/code, bounded
  scrolling, Expand/Open full and long-message presentation are covered.
- Real DPAPI protected-source round trips, 32 reopens, exact Unicode/CRLF bytes,
  ciphertext/ID tamper, malformed UTF-8 and size/read bounds pass.
- TransferManager durable restart, source replacement, duplicate/reconnect,
  no-overwrite, failure/cancel and scoped route isolation pass.
- 102 real Schannel/shared endpoint lifecycles and bidirectional binary transfers
  pass. Windows chat attachments/long text/image and paired archive/catalog tests,
  including five profile worker restarts, pass.
- One initial archive test missed its immediately-polled asynchronous startup
  rejection event. An unchanged rerun passed all assertions; no assertion or
  production archive behavior was changed. Timing sensitivity remains recorded.
- Full Windows build/import audit; dev.107 -> dev.108 version/Development updater
  safe extraction/hash audit; Android ARM64/JNI/dependencies/release-hook audit;
  git diff --check pass. No physical execution or acceptance is claimed.

## Preserved, unpublished packages

Windows: `build/development/4.0.2-dev.108/DevelopmentUpdater/PeerChat-4.0.2-dev.108.zip`

SHA-256: `2d5df5ddde8f751e81177d643127943addd5042e1aaddc7122e5631a658702a9`

Android directory: `build/development/4.0.2-dev.108/Android/`

- Debug: `5b5b56fc83690edb9e55ae87a88fb927e63c0da63d803fc6c0090d5b20f445ad`
- Test: `e4fcac9fa788d8fe682b0a5cad85fbdbe66c6cd95aa9b356512033d5b6c9a8c5`
- Unsigned release: `b8a3fa66b1b42616fbca725a14bccce3d1aea3e527905ab40308721effa924ae`

Pending physical tests: Android/Windows pictures/files both directions; restart
mid-transfer; long prose/Unicode/code exact Copy/Open full and restart; saved
original file hash; rejection/cancellation. No install, deployment, merge or
publication performed. Unrelated object files remain excluded.
