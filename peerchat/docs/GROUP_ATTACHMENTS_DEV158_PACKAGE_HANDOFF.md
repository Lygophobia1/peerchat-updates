# dev.158 Windows group-attachment package handoff

## Repository

- Branch: `codex/native-unicode-emoji`.
- Worktree: `C:/Users/Livel/.codex/worktrees/native-unicode-emoji/Create 2D Game`.
- Resumed clean baseline: `de3b643`.
- Accepted dev.148 ancestry `f444265` verified; accepted dev.153 and previous
  physical-test packages retained unchanged.
- Implementation: `8c24b4b`, `02fda44`, `cfc3fa2`, `f3b057f`.
- Package source checkpoint: `6803c3abfd8fa0cc5d51abbc68d0f4d29071b09d`.
  The final handoff commit only completes this report and the packaging audit
  script; compiled application/server source remains identical.
- Final HEAD is the commit containing this handoff; obtain it with
  `git rev-parse HEAD`. It cannot embed its own hash.
- Final tracked/untracked status: clean after the handoff commit. Ignored outputs
  and isolated diagnostics are preserved. Loose compiler objects were moved,
  not deleted, into dev.158 `ScreenMedia/compiler-objects`.

## Deliverables

All paths below are relative to this worktree's `peerchat` directory.

| Artifact | Size | SHA-256 |
| --- | --- | --- |
| `build/development/4.0.2-dev.158/PeerChat-Windows-4.0.2-dev.158.zip` | 12198645 bytes | `e305dc4ec89861535b75bed2209660f4cb5fd802a700d9895457fffef0be074e` |
| `build/linux/4.0.2-dev.158/PeerChat.Server-4.0.2-dev.158-linux-arm.zip` | 34323291 bytes | `cdfd255e3057f27701d50a6f1013acac5122845921e605fb99cb63c657414acc` |

ARM64 and x64 server packages also exist in the same Linux version directory;
their hashes are recorded in `build/development/4.0.2-dev.158/package-audit.json`.
The Windows server executable/configuration remain in the versioned `Server`
directory; the WPF Admin directory includes the matching server/configuration.
The Windows client ZIP includes validation/deployment documents and a disabled
cache-settings example. Linux's directory has the deployment document/example
beside its runtime-only archives and per-RID manifests.

The client ZIP was reopened and every file count and byte hash checked against
the staged payload. `candidate-manifest.json` records source/payload hashes.
Native dependency, HPKE notices, media ABI and default/candidate gate audits pass.
Deliverable servers contain no test fault injection. Cache remains disabled by
default and requires explicit administrator configuration/storage ownership.

## Validation and remaining acceptance

Full details, root causes, changed components and evidence:
`docs/GROUP_ATTACHMENTS_DEV158_VALIDATION.md`.
Shared interface/Android dependency record:
`docs/TRANSFER_CACHE_INTEGRATION_MANIFEST.md`.
Installation, backup, smoke tests and rollback:
`docs/GROUP_ATTACHMENTS_DEV158_DEPLOYMENT.md`.

Final actual Framework server entry-point test PASS:
`F:/PeerChat-isolated-cache-tests/application-ca9f1320681843cdbba196fd281d259a`.
Actual portable candidate entry-point test PASS:
`F:/PeerChat-isolated-cache-tests/application-48bf62c2da5a4915a51241e9927242f4`.
Both use production Windows Application/TransferManager, native HPKE and pinned
TLS with two independent recipients, sender offline, restart recovery and signed
plaintext verification. They are controlled host fixtures, not physical machines.

Windows UI/regression, transfer/history/security, server compatibility, emoji,
History Sync and media coexistence evidence is recorded in the validation report.
Known final compiler warnings are documented; no failing final test is waived.
Dev.156 and incomplete dev.157 Windows build directories are intermediates, not
the final deliverable, and remain preserved rather than overwritten/deleted.

Physical group delivery and Linux runtime/deployment acceptance are PENDING.
Android cached group attachments are unsupported. Current Pi readiness has not
been asserted from stale storage observations; rerun the documented read-only
checks against the actual service/mount before approving installation. No live
Pi service or phone was accessed/modified in this completion pass.

Next action: user reviews the candidate/plan, authorizes a controlled Pi update,
then physically tests picture/file upload once, two independent/offline recipients,
saved hashes, restart/cancel, older history and messaging/media nonregression.
Do not resume another roadmap milestone, merge, or deploy automatically.
