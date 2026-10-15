# Active PeerChat development baseline

By explicit user acceptance on 2026-10-14, the active development line is
`codex/native-unicode-emoji` in the existing `native-unicode-emoji/Create 2D Game`
worktree. Accepted emoji baseline: `0709b5b7afded948956069e5ba774910ab88ab5b` (dev.150).

`f444265607196fb045d311baa44da1e230817636` is a verified ancestor, including dev.148
screen pacing/diagnostics and its preceding mailbox error-owner fix `4de662d`.
The dev.148 worktree/branch, checkpoint and Windows ZIP remain unchanged for rollback.
The emoji worktree was clean before the pipeline audit began.

Inspected all registered worktrees and branch tips before continuing. No newer
scheduler/voice branch or uncommitted scheduler implementation was found to integrate.
Older dirty work in `71be`, `windows-android-unified` and `build-workflow-proof` is
preserved in place; it is not folded into this line. `network-cleanup-phase1` remains
at dev.148 with its three pre-existing generated object files untouched.

The newer explicit branch-unification instruction supersedes the pasted audit
request's earlier separate-worktree/emoji-exclusion instructions. All three audit,
scheduling and voice phases now run here in separate logical checkpoints. No other
branch is renamed, reset, merged or rewritten. Android/Pi deployments remain excluded.

## Physical media acceptance and deferred work

On 2026-10-14 the user physically accepted dev.153 voice calls and ordinary
desktop screen sharing. Preserve that behavior during transfer/cache development;
this acceptance does not cover smooth video playback or shared computer audio.

The future media milestone has separate validation checkpoints:

- Improve active-video smoothness by tracing capture, encoding, RTP arrival and
  recovery, decoding and presentation gaps. Validate sustained 30 FPS motion and
  evaluate optional 60 FPS only where supported; average FPS alone is insufficient.
- Add optional Windows system-audio sharing using an appropriate native loopback
  mechanism. Keep microphone and shared-audio volume/mute independent, preserve
  AEC, authenticated encrypted media and group participant authorization, and
  prevent feedback or unintended rebroadcast of call participants.

The voice-quality backlog also retains background fan-noise suppression and
individual participant volume controls. These are deferred improvements, not
reasons to reopen the accepted milestone or expand the current transfer/cache task.

The user initially paused transfer/group-attachment/cache development on
2026-10-14 for an urgent Windows/Android messaging regression. The subsequent
Autonomous Android Messaging Repair Then Resume File Transfers instruction
authorizes independent transfer/cache work after the safe Android investigation
and checkpoint, even while physical phone evidence remains pending. Preserve
the unresolved Android investigation and do not claim a repair or acceptance.
The phone must not be installed, launched or reset automatically; the live Pi
must not be modified or restarted.

The latest delivery policy supersedes the earlier deliver-to-both requirement:
prefer Windows, fall back to Android after bounded failure, and reconcile other
devices through History Sync. Exact cross-device delivery/notification ownership
needs an approved design before extending the existing independent mailbox ACKs.
See ANDROID_MESSAGING_REPAIR_DEV155.md. Cross-device media handoff remains future
work, without enabling Android video or screen capture.

The paused cache work includes a disabled durable SQLite ciphertext runtime,
pinned storage adapter, signed descriptor parsing and initial roster admission.
Isolated storage tests passed up to 10 MiB, independent recipient progress,
incomplete/corrupt rejection and actual process termination before upload and
cleanup commits. Admission and client integration are unfinished and unvalidated;
no cache endpoints are enabled and no live server data has been changed.
