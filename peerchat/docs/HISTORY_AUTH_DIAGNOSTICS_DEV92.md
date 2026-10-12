# History authorization failure isolation — dev.92

## Physical evidence and limits

The account-2 dev.90 reports show two current verified self-grants and no cross-device grant. Both authorizers are trusted; server/local grant IDs and generations agree. Admission correctly rejects the pair as `incompatible_authorizers`. No archive transfer has started. The underlying reason why the user's explicit cross-device authorization did not produce a grant is not proven.

The retained connection popups identify selected friend accounts 3 and 5, not the account-2 history device pair. Code inspection establishes a diagnostic defect: rejected native archive commands use the generic Error notice with no archive identity, allowing the error to become a selected-friend Connection failure. “Identity verification required” in those popups is therefore not proof of a friend identity failure or of the grant-creation stage.

## Narrow correction

- Native archive command failures remain archive-scoped (`archive-control:operation`), rather than changing a selected friend's connection/failure projection. The original exception and fail-closed C ABI result are preserved.
- The managed archive adapter records bounded native-operation or server-request failure stages. It does not log request bodies, proofs, signatures, keys, credentials, messages or attachments.
- Failed Settings actions retain action, local/target device, account, authentication generation, stage and exception type in full/Copy History diagnostics. Later metadata refresh does not erase this last-operation record. It is displayed only in its matching authenticated account/generation.
- A user action blocked by an existing operation reports busy. A coalesced read-only refresh does not overwrite the current completed/live status.

No grants are synthesized, reissued or silently trusted. No identities, custom permissions, storage, wire formats or transfer policy change. Existing durable publication retries remain owned by the existing path.

Useful stages: `native_operation_10` is the scoped explicit consent/grant command; `native_operation_2` is authenticated metadata admission; `server_archive_grant_register` is publication transport; `server_archive_authorization_state` is the metadata request. A validation/projection exception without a delegate failure is explicitly distinguished. These boundaries narrow the failure but do not by themselves prove a particular native validation condition.

## Validation / candidate use

Packaged regressions cover archive-error classification, explicit managed cross-device consent followed by verified paired transfer, native consent failure-stage retention, publication failure-stage retention, durable retry, account/generation failures and worker restarts. Existing grant/storage/scoped-TLS security and UI/layout tests are rerun, plus full Windows build/import and version/updater audits. Build and unchanged security-suite logs are `build/tests/dev91-build.log` and `dev91-security.log`; final packaged evidence is `build/tests/dev92-*.log`. The initial dev.91 diagnostic build is superseded, not overwritten, because UI testing caught a busy refresh overwriting completed status; the correction is packaged in dev.92.

The unchanged Android build is regenerated only for the shared-version audit; no phone installation or Android UI change is performed. No publication or merge.

Physical next step: on device `71ac258c…`, Manage the other device `ab972281…`, explicitly authorize selected permissions and confirm once. If it fails, copy full History diagnostics immediately. Inspect `History Sync last settings operation` and archive-only timing events before trying again. Do not revoke, re-enable authorizers, clear history or repeatedly issue replacement grants.

This is a diagnostic physical-test candidate, not a claim that the missing cross-device grant is fixed or History Sync is accepted.
