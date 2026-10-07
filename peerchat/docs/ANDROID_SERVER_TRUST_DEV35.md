# Android server trust correction — dev.35

The physical protected-record probe found that dev.34 did persist/reopen the
accepted LAN endpoint (192.168.1.6:7443) with the Pi certificate SHA-256
62A181282FE940F03F6A35541220A05F9ED2CB85A41F6DE3B0DF111286BA17E3.
Remember Me independently retained the public endpoint (173.225.14.37:7443)
bound to the superseded F9B982096E8A1D9ED79D97EFC27F97435564466BBC860B0541FD56FA52463154
certificate. Read-only TLS inspection found the public endpoint now presents
the Pi certificate too. Credentials are not portable between those identities.
No protected-record write failure or silent pin replacement was demonstrated.

Corrections:

- Settings form refreshes its local fields after authoritative saved state changes.
- Endpoint-specific trust status distinguishes Untrusted, Trusted and Certificate
  changed; closing a mismatch prompt does not erase the mismatch indication.
- Manual colon/space-formatted fingerprints normalize before length validation.
- Casing/whitespace-only host edits retain the stored endpoint spelling used by
  history/account indexes. LAN and public IPs remain distinct logical endpoints.
- First-use sign-in continues once, only after explicit approval, certificate
  verification, protected save and successful readback. Cancel or readback failure
  prevents continuation. No automatic certificate-mismatch override.

The operator approved retiring only the obsolete public-endpoint remembered
authorization. A separately gated instrumentation operation requires the exact
old endpoint/pin, then uses the existing authenticated tombstone and verifies
server configuration is unchanged. This is not automatic credential rebinding.
The phone disconnected before that operation/install: retirement is PENDING.
No normal identity, recipient key, history or server configuration was modified.

Validation: 94 JVM/Compose/presenter tests pass; debug, test and release APKs,
lint, ARM64/JNI/dependency audits pass. Full Windows build/package/import audit
passes at 4.0.2-dev.35. One host TLS fixture now temporarily isolates Android
Conscrypt provider contamination and restores its order; production TLS unchanged.
Physical replacement install, scoped retirement and fresh password enrollment
remain pending. Existing dev.34 artifacts are preserved; no release published.
