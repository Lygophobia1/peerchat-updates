# Independent Android direct-message multimedia candidate

Checkpoint: `7431568`, version `4.0.2-dev.116`. This candidate preserves the
dev.107–109 native TransferManager picture/file/typed-text work. Group enrollment
is disabled; group completion is not required for these tests. Physical acceptance
is **pending**. Nothing is installed, deployed or published by this document.

Android debug APK:
`build/development/4.0.2-dev.116/Android/PeerChat-4.0.2-dev.116-debug.apk`

SHA-256:
`0b578e19d4c00fa831062b564f30a3cdaab2c753fa9a182af4322a2d5329df02`

Matching Windows client:
`build/development/4.0.2-dev.116/Client/PeerChat.Desktop.exe`

When physical installation is requested, replacement-install only with
`adb install -r <APK>`. Never uninstall, clear app data, recreate keys, remove
Remember Me or reset history. Verify the same existing identity after launch.

## Physical sequence

1. Send a picture Android → Windows and Windows → Android. Accept once, verify
   inline thumbnail, full-size view and exact original bytes after saving.
2. Repeat with a Unicode-named general binary file and an empty file. Check
   offer/accept/progress/completion; decline/cancel an additional test transfer.
3. Interrupt one transfer, restart the app normally and verify native resume or
   an accurate recoverable state, without duplicate attachment cards.
4. Send long prose, multiline Unicode/CRLF and code-like pasted text in both
   directions. Verify bounded scrolling, expand, full view and exact Copy output.
5. Cold-launch and reopen conversations: completed pictures/files/text remain
   available; history, identities and remembered authorization are preserved.

Record platform, version, direction, completion status and byte/hash comparison.
Do not log private keys, tokens or ordinary user content. Use test-only content.
These are direct authenticated transfer tests, not offline file-mailbox tests.
Host/JVM tests and artifact audits do not substitute for this phone acceptance.
