# Android password device bootstrap correction

Physical trust acceptance on the Motorola, 2026-10-07:

- Saved LAN endpoint 192.168.1.6:7443 already trusted the Pi certificate.
- Obsolete remembered authorization instead referenced 173.225.14.37:7443
  with the old Windows certificate. The separately approved, narrowly gated
  retirement test removed only that obsolete authorization; protected server
  configuration, identities, recipient keys and history were preserved.
- Pi certificate SHA-256 remains
  62A181282FE940F03F6A35541220A05F9ED2CB85A41F6DE3B0DF111286BA17E3.
- Replacement install, successful login and close/reopen without another
  trust prompt were confirmed by the user. The Pi database subsequently
  contained one durable FCM registration for the phone account; Android
  reported authenticated live presence.

The subsequent misleading sign-in rejection was not a TLS/password failure.
Bounded request diagnostics identified messaging_bootstrap returning
device_account_mismatch. The current phone identity was not yet registered
on this server. Password login creates an account session, not device ownership;
the combined bootstrap correctly requires already-established ownership.

Password startup now invokes the existing authenticated presence/device
registration operation before the combined bootstrap. No identity is generated,
replaced or copied by this correction. The existing server enforces exclusive
account ownership of registered device IDs. Remembered startup remains unchanged.
No server production code or deployment changed.

The server regression proves unregistered bootstrap rejection, successful
bootstrap after authenticated registration, and rejection of attempts to
register/bootstrap a device owned by another account. Request diagnostics emit
only allowlisted operation/error categories, never arbitrary payloads or secrets.

This does not implement explicit certificate-replacement UX. Existing stored-pin
mismatches still fail closed. It also does not establish end-to-end FCM delivery
acceptance merely from successful push registration.

Validation: discovery integration (including the new device-registration
regression, RPC binding, ownership, event leases, reconnect and restart), Android
JVM suite, debug/test/release builds and lint, ARM64/JNI/TLS dependency audits,
full Windows build/package/import audits and diff checks passed. The final
immutable package is 4.0.2-dev.42. Version/updater extraction audits passed
against dev.41. Development ZIP SHA-256:
28be058bab2a73a1393bc936f15a14edb1aa13006c7dc15800c91d55dfa9ea06.

Physical acceptance was reported for the same correction in the interim
dev.41-labelled diagnostic APK before final version allocation. This distinction
does not relabel or overwrite the preserved dev.41 artifacts.
