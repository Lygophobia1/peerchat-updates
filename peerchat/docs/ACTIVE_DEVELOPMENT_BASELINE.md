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
