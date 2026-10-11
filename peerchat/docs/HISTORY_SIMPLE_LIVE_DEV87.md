# Simplified History Sync and committed progress — dev.87

## Evidence and correction

The user physically confirmed that VMTEST received all 22 missing messages with
the preceding archive implementation. This does not physically validate this UI.
The Windows Settings refresh was subscribed to the voice timer, which stops when
voice is idle. Settings therefore missed updates despite successful native imports.

Settings now subscribes to application HistoryPresentationChanged notifications,
with a bounded account-context check on the existing application timer. The native
reader reports newly inserted messages only after verified durable commits;
duplicates and scanned records are not presented as received messages. A separate
protected, account/device-bound presentation record preserves last successful sync.
Internal application operation 23 adds these fields without changing archive wire
messages or operation 13. Durable history revision invalidates conversation paging
and reloads the open conversation independently of peer-route availability.

## UI and security

Windows and Android show one History Sync switch, This Device / Other Devices,
source/phase, actual new messages received this session, completed conversations,
and last success. With no trustworthy total, active progress is indeterminate.
Add/Approve, Manage, Sync now and appropriate Retry remain accessible. Raw
permissions and authorizer controls are Advanced. Existing custom grants and
automatic-sync preferences are preserved; the master switch never issues grants.
Offline Messages remains separate. Availability is unknown unless actually known.
Windows callbacks require the current owner, authentication generation and UI
epoch. Android results must match the current authentication generation.

## Automated validation

- Archive enrollment/security, history storage, stale/revoked/mismatched grants,
  session scope, server metadata and peer TLS regressions.
- Rendered WPF live batches with no active voice timer, actual received counters,
  last-success display and stale owner/generation/operation rejection.
- Actual paired packaged Node workers: 25 messages, verified imports, durable
  history revision, last success, scoped relay/TLS and five worker restarts.
- Compose/JVM live progress and explicit approval tests, operation-23 decoding,
  preservation of raw grants when the master switch is OFF.
- Android debug/test/release and lint; ARM64/JNI/dependency audits.
- Full Windows build/import audit and Development version/package extraction audit.

Development candidate only, not published. dev.86 was an intermediate local build;
the normal immutable build workflow allocated dev.87 for the final Windows build.
Android and authoritative version metadata are aligned with dev.87.

Physical retest: leave Settings and a conversation open while importing multiple
batches; check advancing counters and conversation contents without reopening,
then cold-launch and verify persisted history and last success. No physical
acceptance is claimed for the new presentation.

## Next group-history security boundary

GroupKeyManager::apply rejects duplicate account IDs in a roster, and group
MessageLayer::context requires the exact local account/device in that roster and
the signed ledger. Server GroupRostersForDevice likewise selects the exact device.
An archive grant for another device of the same account is not group authorization.
Current roster records also do not provide historical membership proofs for old
roster/key generations. Do not relax these checks or import attested plaintext as
though its original group signature and membership were independently verified.
Group history requires an explicitly designed authorization/provenance prerequisite
before cross-device imports; existing grants alone must not silently enroll a device.
Live group diagnostics can proceed independently using existing session snapshots.
