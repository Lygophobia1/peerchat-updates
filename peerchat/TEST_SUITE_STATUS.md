# PeerChat test-suite status

The normal development build gate keeps focused, current coverage close to the
feature being changed. Historical tests remain for review; they are not deleted
because their assumptions no longer match the present architecture.

## Current

- Native/desktop compilation and staged package dependency audit.
- Focused diagnostics retention/profile regression.
- Updated WPF smoke with unique per-run result files and child exit status.
- Updater/package verification relevant to a packaging change.

## Legacy or needs review

- `storage_test`: `Altered ciphertext was accepted`; outside diagnostics gate.
- `AdapterTest`: `Share format`; requires adapter-contract review.

Neither is removed or weakened by this build-environment checkpoint.
