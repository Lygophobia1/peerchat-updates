# Source-side archive rejection — dev.129

Read-only constant-stage diagnostics now distinguish source control decode,
serving lease, history manifest/storage, device/account mapping, protected record
match, permission and attestation signing. All authorization and import checks
remain unchanged. Diagnostics retain failures without clearing them on an
unrelated later successful manifest response; they contain no record contents.

141 Android JVM/UI tests; debug/test/release/lint; ARM64/JNI/dependency audits;
native provenance, 67 durable history imports/restarts, mutual TLS archive and
negative missing-directory diagnostics; actual packaged Node import/attachment
and unchanged-friend-refresh regressions; full Windows build/package/import,
version and updater integrity/extraction tests pass. Android was replacement
installed without clearing profiles, identities, grants or history.

The real Motorola dev.129 report proves the remaining source rejection is
source_record_device_mapping: a stored record references a sender or recipient
device without an authenticated historical account mapping. Both signed archive
grants and the current peer route are valid. Windows then loses its source and
retries; suppressing relay failures would not fix the missing evidence.
The next step is to identify the missing public device reference and reconcile
only authenticated retained/server metadata. No owner may be inferred from
plaintext or fabricated. Physical full History Sync acceptance remains pending.

Android debug SHA-256:
2F60EB83724DAB5A757A649D46CB61C7D7FB44C628311B06385B8D380AC83AC4
Development updater ZIP SHA-256:
26393264e73c13001eca9f9da3c0e85cf1e470dfb3dfeee276ad1389ea42d925
Candidates: build/development/4.0.2-dev.129/{Client,Android}.
Pi remains compatible dev.125; no version-only deployment, merge or publication.
