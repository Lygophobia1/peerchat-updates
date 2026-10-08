# Durable file-transfer restart resume

## Paperclip accessibility correction (dev.43)

The composer paperclip opens the existing File transfers window. Its layout
still had a legacy disabled flag and unavailable tooltip after the transfer
handler was added; this made normal mouse/keyboard activation impossible.
Removing that flag restores the existing handler without changing transfer,
authorization, session or networking behavior. Sending still requires selecting
a verified direct conversation and explicit file selection; receiving still
requires explicit acceptance. Drag/drop and Android file UI are not implemented.

The focused UI regression now checks enabled, hit-test and keyboard-focus state
before raising a click. It reproduces the defect against dev.42 instead of
bypassing the disabled control and incorrectly passing. The obsolete general
UI-smoke assertion requiring attachments to remain disabled is corrected too.

Validation passed: two simultaneous isolated Windows client UI processes,
transfer manager and durable restart regressions, voice-settings UI assertions
with a retained native process handle, full Windows build/package/import audit,
94 Android JVM tests, debug/test/release builds and lint, ARM64/JNI/dependency
audit, version/updater extraction audit and diff checks. The transfer UI runner
now retains its process handle before waiting so fast successful exits do not
surface as null exit codes; it still requires an actual zero exit status.
Dev.43 Development ZIP SHA-256:
e17c576043bf073c33ce6a3602488832f1d3b7657035a5bb9b6bcaf5a4548a78.
Physical end-to-end file acceptance remains pending.

TransferManager remains the sole bulk-transfer owner. Its optional StateStore
persists transfer snapshots; it does not own sockets, reconnect, delivery or chat
history. Windows Node still accepts only the verified, DirectMessage-authorized
SessionTransportManager winner with the file-transfer capability.

## Protected state and crash ordering

Windows DurableJournal stores versioned snapshots beneath the existing profile's
transfer-resume directory. DPAPI context binds the exact profile directory and
existing device Peer ID. No new key, schema or wire format is introduced.
An exclusive owner.lock prevents simultaneous transfer writers for one profile.
Snapshot replacement uses a flushed temporary file and atomic write-through
rename. Unchanged snapshots do not rewrite the file.

Records contain transfer ID, authenticated remote device, basename, length,
SHA-256, fixed 32-KiB chunk parameter, contiguous accepted offset, submitted
high-water bound, source/partial identity and protected paths, lifecycle state and
creation/update/absolute expiry. Account authorization is re-evaluated through
the existing authenticated friendship/session path, never inferred from a saved
route. Routes, TLS connections and attempts are not serialized.

Receiver append and file flush precede durable progress and offset ACK. A crash
after append but before journal commit leaves an unacknowledged tail; reopening
the same file identity truncates that tail to the committed offset. Sender
high-water is committed before chunk submission, so an ACK from the previous
winning route can be accepted after restart.

Source reopen checks volume/file identity, size, basename and last-write time.
It then incrementally rehashes the entire source before offering or sending any
resumed bytes, even if metadata matches. An early Resume cannot bypass that
rehash. Changed sources fail under the original transfer ID.

Receiver reopen verifies protected metadata and the exact owned file identity,
rehashes the committed prefix in bounded 32-KiB iterations, and resumes missing
bytes. Final whole-file SHA-256 remains mandatory before no-overwrite rename.
If a process died after a verified atomic rename but before recording Complete,
recovery recognizes the same file identity at the final path, re-verifies the
whole file and answers the sender's retried Finish. It never overwrites a final
file or presents an unverified partial as complete.

## Failure and retention

Normal app exit retains active partials and their protected state. Transient
route loss still uses the existing session owner. Local file I/O failures pause
with retained state for restart; no file contents are logged. Journal failures
withhold all affected ACK/send work and retry on TransferManager's existing tick,
at most once per 250 ms while failing. They do not close messaging sessions.
Node emits at most one storage-wait diagnostic per profile open.

Explicit Cancel, integrity failure or absolute seven-day expiration removes only
the owned partial through its exclusive handle. Expiry is checked during service
and reopening, not extended indefinitely by reconnects. Missing or replaced
files fail closed; unknown/replaced files are never deleted. Cleanup failures
report manual attention rather than pretending deletion succeeded.

Limits remain four active/resumable transfers, 64 retained transfer records,
256 MiB per file, one outstanding 32-KiB chunk per transfer and existing reliable
queue backpressure. Old terminal records are evicted before new entries; paused
resumable entries are not evicted to make room.

Legacy dev.40 cleanup records did not contain enough metadata to resume and keep
their original authenticated cleanup behavior. New transfers do not use that
deletion-only journal. A crash before the first snapshot can leave an empty
partial, but no content is written before acceptance is durably recorded.
Unknown empty files and interrupted snapshot temporaries are not guessed into
resumable state. Physical two-PC restart tests remain pending.

## Automated coverage

Existing binary/Unicode/boundary/hash/cancel/limit/reconnect tests remain.
New tests cover actual subprocess exit without destructors, sender and receiver
reopen, source identity and content changes (including preserved timestamps),
early Resume during revalidation, uncommitted tails, missing partial, duplicate
chunks, crash after rename, DPAPI tamper, wrong profile/device, seven-day expiry,
storage failure withholding ACK and later recovery, and explicit cancellation.

The real Node/Schannel opaque-relay fixture now closes and reopens both profiles
mid-transfer, checks unchanged Peer IDs and transfer IDs, re-establishes mutual
TLS through SessionTransportManager, also replaces a live relay route, verifies
the final 2-MiB Unicode-named file, then verifies normal text/ACK coexistence.
These are automated host tests, not physical device acceptance.

## dev.41 checkpoint validation

Passed the focused manager/restart suite (including the destructor-free child
process), the real Node/Schannel restart-and-relay fixture plus two additional
executions, 1,000 shared reliable-wire lifecycles, and the complete portable
Storage/Profile/CNG/DPAPI/history/outbox/ACK/dedupe/registry/group-envelope suite.
Android ARM64 NDK syntax compilation passed; 94 JVM tests, debug/test/release
builds, lint and JNI/ARM64/dependency artifact audits passed. WPF file-transfer
and existing Voice settings/AEC smoke passed. Full build.ps1 -All and packaged
restricted-load/import checks passed with existing unrelated compiler warnings.
Version/updater safe extraction dev.40 to dev.41 and git diff --check passed.

Windows package:
build/development/4.0.2-dev.41/DevelopmentUpdater/PeerChat-4.0.2-dev.41.zip
SHA-256: 7eb2cc5433bfd265232c506ef736b75f767d02ae61f27428f0fcb986d72689f7

Android APKs: build/android/4.0.2-dev.41
Debug SHA-256: 68f0270f1154ef56a78d16761ddfc6841168d355c73f4e194aebb81685f85caa

No publication or device install. No server source/deployment changes.
Pi dev.34 PID 15754 remains listening on 7443; coturn PID 2718 is untouched.
