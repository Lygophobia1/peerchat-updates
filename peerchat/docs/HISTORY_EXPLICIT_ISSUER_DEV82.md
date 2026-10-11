# Explicit issuer grant action — dev.82

The two physical dev.81 snapshots confirm both metadata views are fresh and
agree on all three grant IDs/generations. VMTEST (0612f258...) trusts both
authorizers and can attempt PC -> VM using the PC-issued pair. PC (0cb421c4...)
has disabled its own authorizer, so it cannot admit that pair. The VM-issued
grant for PC is valid, but VM has no VM-issued self-grant.

The UI gap is proven in HistorySettings: ON is an aggregate over trusted issuers,
and Save compares against those same aggregate flags. Saving unchanged ON
permissions therefore does not request a self-grant under the local issuer.

Dev.82 adds a separate explicit action in the focused Manage/setup dialog:
`Authorize selected permissions from this device`. The action appears only when
the local device is an enabled trusted authorizer in the current projection. It
uses existing run(5), explicit confirmation and native signature/consent/account/
freshness checks; the UI never creates a grant itself. Missing authentication
fails closed. No grant is issued on dialog open, refresh, ordinary unchanged Save
or reconciliation. Disabled trust is never re-enabled. Existing authorizers and
grants remain unchanged until the user explicitly confirms the new action.

Physical next step on VMTEST, not PC:

1. Run dev.82 with the existing profile.
2. History Sync -> This device -> Manage.
3. Leave Receive and Provide ON.
4. Click `Authorize selected permissions from this device` and confirm.
5. Leave both clients signed in; verify the VM-issued self-grant on server and
   both diagnostics, then admission/transfer in both directions, restart/catch-up
   and duplicates. Do not re-enable the PC authorizer.

Automated checks passed:

- WPF rendered explicit control present for enabled local issuer, absent for
  disabled issuer; unauthenticated click is contained with no grant or dialog
  deadlock. Existing History Settings smoke passed.
- Enrollment, history storage, scope and authorization security regressions,
  including incompatible issuer, stale/wrong-account/revoked grant rejection;
  bidirectional native import, durable cursors/restart/dedupe passed.
- Actual paired packaged Node/mutual Schannel TLS history/attachment transfer
  and profile-worker restarts passed.
- Full Windows build/package/import and Development version/updater hash/safe
  extraction passed.
- Android JVM/debug/test/release/lint and ARM64/JNI/profile/TLS dependency audit
  passed. No phone install, server deployment or publication.
- git diff --check passed. Three unrelated untracked object files excluded.

Windows: `build/development/4.0.2-dev.82/Client/PeerChat.Desktop.exe`

Development ZIP SHA-256:
`923e88fc0708680f2c5691f5965c97504b2094a58d6580f796a5c8c12fce6eb6`

Physical authorization and bidirectional transfer remain pending; no real user
grant has been issued by this checkpoint.
