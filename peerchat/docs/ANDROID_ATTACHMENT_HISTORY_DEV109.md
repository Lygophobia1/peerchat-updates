# Android completed attachment history — dev.109

Completed pictures/files/long-text previews now read the existing protected
AttachmentCatalog as well as the temporary TransferManager journal. A restart or
journal expiry no longer removes the recent completed cards and their local
readable copies. The native readText command also resolves a catalog copy after
its journal entry disappears. Protected typed-text sources retain their existing
purpose/ID-bound token and provider; received copies retain validated private-cache
source tokens. No sender/peer/server connection is needed to read local copies.

This is a bounded recent window, not unlimited attachment pagination: at most
64 catalog records and 64 final projected cards. Active transfers have priority.
Catalog metadata is cached for at most two seconds; a new watermark refreshes it
immediately. Existing custom permissions, archive grants and cursors are unchanged.
The readout rechecks the profile's protected historical account/device bindings;
missing mappings/copies never become arbitrary readable paths. Actual export/view
still reopens the validated local source. No remote path, transport admission or
completion ACK is invented.

Validation:

- Protected catalog CAS/pages/account-binding/tamper/revocation pass; a new 70-item
  catalog test reopens real Storage with no transfer journal and verifies the
  bounded recent window, identity and possessed-copy metadata. Wrong-account,
  missing-copy and invalid window rejection pass.
- TransferManager/source/journal restart and exact-route tests pass.
- 127 Android JVM/Compose tests, debug/test/unsigned release builds and lint pass.
- Actual paired Node archive/catalog/Unicode file transfer and five profile
  worker restarts pass unchanged.
- Full Windows build/import audit, Android ARM64/JNI/dependencies/release-hook
  audit, version/Development updater safe extraction/runtime hashes and
  git diff --check pass.

No physical multimedia acceptance is claimed. Additional-device group enrollment
remains disabled. No deployment, install, publication or merge performed.

Unpublished Windows package:
`build/development/4.0.2-dev.109/DevelopmentUpdater/PeerChat-4.0.2-dev.109.zip`

SHA-256: `54d686568d2a918d0ce12f6d4b737a9928f45067531d1b8258a5c54d58b36503`

Android files preserved under `build/development/4.0.2-dev.109/Android/`:

- Debug: `4db0cdefbb70a34af046992349b31a7c52c47cd98d7d89364274a177f659740f`
- Test: `e4fcac9fa788d8fe682b0a5cad85fbdbe66c6cd95aa9b356512033d5b6c9a8c5`
- Unsigned release: `a580f12bdd86590a843359b4a8d1643b2a244816b5ced602aeaf696a2622d60c`

Physical steps remain: bidirectional picture/file transfer, exact saved hash,
foreground/background restart mid-transfer, retained preview/export after reopen,
long prose/Unicode/code Copy and Open full, acceptance/cancellation and duplicate
prevention. Older-than-window catalog pagination remains a later bounded UI task.
