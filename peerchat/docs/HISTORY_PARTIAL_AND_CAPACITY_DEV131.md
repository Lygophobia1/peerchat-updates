# Android source → Windows reader repair, dev.131

Continues `9bae40a` on the existing branch/worktree. Its retired cleanup and
deadline ownership fixes remain intact. The read-only protected Windows policy
probe confirms account 4, reader `35049f3bf26f1d01a7e6c2d0dad2132a646093008d0c2f23f96fa32aa418201b`,
trusted Android authorizer/source `61f70fb01a6055499eed089025e8e8e2850d640b4c4a6ba3ad772168f092b51b`,
matching generation-1 grants, and that exact active source intent. The separately
selected Jonah/account-3 diagnostics are not used for this operation.

## Proven corrections

An unmapped historical device caused source enumeration to throw and close an
otherwise authorized archive transport. Retrying could not repair attribution
and blocked later verified conversations. New clients negotiate capability 4096;
request kind 11 explicitly permits a bounded unresolved-conversation response
(kind 10). Old peers continue using kind 2 and retain their fail-closed behavior.
The relay still forwards the same bounded opaque peer-TLS bytes; no server update
or deployment is needed for this extension.

Only missing authenticated sender/recipient mapping may defer a conversation.
Signature, scope, account, grant and storage validation failures still reject.
The refusal is accepted only on the current authenticated archive route and bound
to the exact pending request, source store, conversation and cursor window.
Its count describes all remaining deferred rows in that conversation window,
including any verified prefix withheld with the failed page. It is source-reported
accounting, not evidence of message ownership or an exact count of corrupt rows.

The protected refusal record and scan accounting are durable. The conversation's
import cursor does not advance. Manifest traversal may continue to other
conversations, importing only normally attested and verified records. A later
scan retries the blocked window. No unresolved bodies are serialized, assigned
to an account, imported, erased or silently declared synchronized. Partial scans
display imported and unresolved/deferred counts, rejected-batch count and Partial;
they do not update the last full success time or display Complete/100%.

The four historical references/236 indexed rows documented at `ab570f4` remain
preserved. This repair permits eligible conversations to progress independently;
it does not recover or migrate those identities. Attachment-catalog provenance
still fails closed if its metadata cannot be attested.

The transfer-capacity regression independently failed with `File transfer limit
reached`: a fifth well-formed offer retired a valid authenticated endpoint when
four offers awaited acceptance. Capacity now produces an existing Cancel packet
for that validated transfer ID, without allocating another active transfer or
altering the four retained offers. Windows and portable Android endpoints remain
usable. Malformed/unauthorized packets and real journal failures retain their
existing handling. This explains a misleading application-level TLS failure;
it is not proof that it caused every physical archive closure.

## Validation

- Native partial fixture: nonempty verified imports, three unresolved source rows
  retained, blocked cursor remains zero, counts survive reopen/retry, no duplicate
  counting, old-client requests fail closed, mismatched refusal rejected, and
  normal verified imports resume when authenticated mapping evidence is supplied
  only in the isolated test directory.
- Actual packaged Node workers and mutual Schannel TLS: 25 committed imports,
  three preserved unresolved rows, Partial with no fabricated full-success time,
  scoped Unicode attachment download and five profile-worker restarts.
- Shared endpoint test: 102 real TLS lifecycles, bidirectional binary transfers,
  capacity refusal preserving the winning endpoint and subsequent text/ACK delivery.
- Existing enrollment, provenance, grant revocation/staleness, archive scope,
  mutual-TLS history, restart/replay/deduplication regressions pass.
- Windows dev.131 client/updater/server builds pass. Android debug/test/release,
  JVM tests and lint pass after correcting the optional accounting parser to use
  the existing `hasRemaining()` accessor. The Android status test verifies Partial,
  committed imports and unresolved counts rather than full completion.

## Limits and handoff

No phone access, installation, key/profile mutation, historical migration or live
Pi deployment occurred. Existing client stage labels such as
`route_closed_before_tls` describe when cleanup occurred, not a unique causal
error. No fresh matching server/client attempt trace was available for those
physical closures; physical correlation remains pending. The source mapping
failure and transfer-capacity teardown were reproduced and corrected, and the
previous connection-ownership regressions remain passing.

Use matching new Windows/Android builds, select the account-4 Windows profile and
start sync **from the Android device row on Windows**. Expect eligible messages
to import, and Partial if legacy attribution is still unresolved. Restart Windows
and repeat to check persistence and deduplication. Do not change grants or infer
old ownership to force completion. Manual phone installation/acceptance remains
the user's responsibility. Later transfer-statistics and automatic-setting work
continues as separately requested, before the final combined physical handoff.
