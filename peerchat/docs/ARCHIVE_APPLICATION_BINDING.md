# Native application command binding — dev.50

The shared Enrollment commands now have a bounded local application ABI. Windows
uses the existing Node profile command queue and CoreAdapter; Android uses the
existing instance registry, profile gate and JNI. Neither frontend owns signing,
grant validation, history, sessions, networking or retry state.

The commands cover exact signed descriptor registration, authenticated metadata
refresh, bounded device views, explicit issuer consent, grant/revoke/disable and
pending publication readout. Local ArchiveApplication/v1 commands are distinct
from encrypted peer ArchiveControl frames; they cannot be sent as peer payloads.
The entire command is parsed before mutation. Inputs and copied results are capped
at 256 KiB. No private keys or history content appear in these commands.

Archive state is worker-confined. Windows initializes it only from a current
authenticated account snapshot and resets it before profile teardown/restart.
Cached prior-profile authentication cannot enable it. Android borrows the existing
native profile lifetime and rejects missing, invalid or signed-out instances.
Both paths invalidate archive metadata when authenticated snapshots change.
Fresh login still requires fresh signed metadata; it does not enable archive trust.

No server deployment change is needed. The validated dev.46 Pi server remains
running on 7443 with the existing data/certificate/TURN/FCM configuration.

## Validation and remaining work

The packaged Windows C ABI regression exercises five real Node profile-worker
restarts, exact descriptor/identity persistence, explicit consent, unpublished
grant denial, server metadata confirmation, wrong-account rejection, logout and
relogin requiring fresh metadata. Shared tests reject oversized, trailing or
wrong-operation commands before policy mutation. Android debug/release artifacts
are checked for the ARM64 JNI export, not claimed as physical execution.

This is not finished user-visible archive sync. Settings device selectors and
consent dialogs, platform RPC servicing of public metadata, archive-purpose route
execution, accepted-history projection/provenance UI and TransferManager-based
attachments are still pending. No normal profile is automatically enrolled.

Dev.50 validation: full Windows build/import audit; Android JVM, debug/test/release
builds and lint; ARM64/JNI/dependency audits; shared enrollment/history/provenance
and real archive peer-TLS regressions; packaged application boundary regression;
normal Node reconnect regression; and dev.49-to-dev.50 updater/version audit passed.
No device execution, server redeployment, publication or physical archive acceptance
is claimed. Development ZIP SHA-256:
`19428cbafc6cf291bf772bff59f67a1cacccb1a3b59fb8364d8299a495e88781`.
