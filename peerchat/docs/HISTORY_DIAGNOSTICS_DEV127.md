# Bounded archive stages and readable Android diagnostics — dev.127

Follows the validated dev.126 handoff/retirement correction (48abd8e).
Physical Windows/Android History Sync remains FAILED / PENDING acceptance;
no new messages have been proven imported on the normal profiles in this pass.

## Read-only changes

The existing native archive execution retains at most eight volatile transport
records containing attempt ID, role, public device reference and a constant
stage. JNI records the receive/dispatch failure stage or manager retirement/
authentication deadline. Windows records how far an archive connection got
before cleanup. No exception messages, credentials, session tokens, keys,
plaintext messages or attachment contents enter these records. Diagnostics do
not change grants, admission, winner selection, retries or durable cursors.

Android Diagnostics was a non-scrolling Column, hiding the lower authorization
and transport details. It now has a bounded viewport with vertical scrolling,
selectable safe report text and a fixed Copy diagnostics action. Copy takes the
complete existing safe owner projection, including build version, not the
visible slice and not a second raw logging/credential source. Live report
updates remain reflected in the next copy.

## Validation

- 141 host/JVM/UI tests, no failures/errors, including 180-line scrolling,
  exact full clipboard content and copying after a live update.
- Android debug/test/release builds and lint; final ARM64/JNI/pinned dependency
  and release containment audits pass.
- Native mutual TLS archive security, verified 25-record imports, exact scopes,
  revocation and bounded diagnostic containment pass.
- Packaged Node workers: actual mutual TLS archive/catalog/Unicode attachment
  transfer, pending cancellation and five profile-worker restarts pass.
- 102 real shared reliable messaging/TLS lifetimes, binary transfers, durable
  ACK/dedupe; 1,000 Hello/identity/Accepted/wire security lifetimes pass.
- Full Windows build/package/import and unified version/updater extraction/hash
  audits pass. No release signature, upload or publication claimed.
- The final debug APK and test APK were replacement-installed on the Motorola.
  A physical UI instrumentation attempt failed with no visible Compose hierarchy;
  the subsequent power/window snapshot confirmed Dozing and keyguard showing.
  That attempt is not counted as passed and security/lock behavior was not
  bypassed. The user subsequently copied and sent the complete real dev.127
  report using the new button, proving physical full-report copying works.

Final debug APK SHA-256:
329A92F4A7623532B2F185588250D5020F8EFF32F1D7BDDE9E47279F92A1E4B0
Windows Development updater ZIP SHA-256:
545e56b6d33686c482afd3800c2ce3c7d4723df0d4798e32084386b570780774
Candidates: build/development/4.0.2-dev.127/{Client,Android}.
Final updater evidence: DevelopmentUpdaterValidatedWithDiagnosticsUi.
The earlier updater evidence is preserved; its Windows runtime/ZIP hash is
identical because the final UI-only correction changed Android, not Windows.

## Physical evidence still under investigation

The earlier dev.126 screenshot showed a completed Windows-to-Android archive
scan with zero new records and an authenticated login; it is not proof that
missing records transferred. The latest dev.127 full report has current verified
grants, authenticated live presence, running intent, no requested archive route,
no incoming archive event, no data rejection and no retained transport failure.
This snapshot needs correlation with foreground/mailbox-only lease servicing
before blaming relay ownership or altering lifecycle behavior. A stable
foreground snapshot is requested without new Sync now or permission changes.

Pi dev.125 remains the tested compatible server; no server code changed here,
so it was not redeployed merely for a client version. Its dev.46 -> dev.125
rollback backups remain at
/opt/peerchat-test/rollback/pre-4.0.2-dev.125-20261013T052808Z.
Normal app data, identities, recipient keys, remembered authorization, signed
grants, histories, attachments and unrelated work remain preserved.
