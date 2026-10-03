# Unified pre-QOL baseline

Validated 2026-10-03 on `codex/windows-android-unified`, based on Windows
`2a5b721ca0e86396271c3bcb06d9f5f37f42db55` and accepted Android
`5481933d4678f0a33480e9522d3e25c697af4fbf`.

The Windows reference worktree and its four modified/six untracked excluded
entries remain untouched. The accepted Android branch is clean. This integration
does not advance either rollback reference or authoritative main.

The only merge conflict was the storage tamper fixture. It retains the Android
stable message-ID targeting and actual-mutation check plus the Windows check that
the protected record is nonempty and valid-sized before corruption.

## No-phone validation

- Full direct Windows build, restricted package loading/import audit and version
  checks passed during the merge proof. Its local `4.0.1-dev.1` label was stale
  merged metadata, not unified version history. The first unified development
  version is `4.0.2-dev.1`; see `ANDROID_QOL_NO_PHONE.md` for its rebuilt artifacts
  and version/updater audit. Windows reference dev.64 remains preserved.
- Portable production Storage, DPAPI, opaque identity (1,000 cycles), SQLite,
  protected records, Profile reopen, history/outbox/ACK/dedupe pass.
- Shared reliable protocol/runtime, 102 real TLS lifecycles, 1,000 stale relay
  attempts, JNI/provider lifecycle and CNG/Schannel interoperability pass.
- Real server login/presence/heartbeat expiry/session replacement, profile reopen
  and queued exact-ID delivery pass. Three authenticated group fanout/room event
  clients, authorization, encryption/tamper and reconnect tests pass.
- Node characterization/reconnect, Adapter model, ProfileManager/retention/privacy,
  voice preferences/presets/group voice and group WPF surface pass.
- H.264 reorder/loss/recovery, accepted one-to-one receive, backend selection,
  shared capture/decode and real local group RTP/MFT fanout regressions pass.
- Android offline debug/test/release builds and lint pass; 45 host/UI tests pass.
  ARM64/JNI/TLS artifact audit passes, including exact pinned BC modules and release
  exclusion of destructive hooks and TLS test credentials.
- Staged diff whitespace check passes. No physical phone/ADB operation performed.

Desktop application/XAML, voice/group-voice, screen/group-screen implementations,
SessionTransportManager and DeliveryManager are unchanged from the Windows parent.
Shared native identity/storage/TLS/protocol boundaries are the accepted Android
extraction, not another messaging engine.

Known deferred Windows checks remain the finite-input screen probe, obsolete
direct-listener Adapter integration and stale UI harness checks. Current model,
transport, media and UI-focused regressions above are separate passing coverage.
Previously reported two-participant physical group messaging/voice/screen evidence
is retained; this run does not claim new physical TURN voice or three-PC/multi-viewer
stress acceptance.

## QOL boundaries identified before implementation

Existing account sessions are expiring and not device-bound refresh credentials.
Remember Me requires a dedicated revocable device-bound credential, protected
locally; no password persistence is permitted.

Direct offline payloads remain in the sender's native durable outbox, not a server
offline ciphertext mailbox. Push can wake an authorized recipient for existing
relay rendezvous/retry while the sender is available. Persisting peer-TLS records
for a later unrelated TLS session would be incorrect. Any stronger server mailbox
requires a separately authorized encrypted-envelope design, not a silent QOL change.
