# Opaque P-256 HPKE: narrowly patched upstream library

2026-10-04, `codex/windows-android-unified`. This is a tested crypto
foundation, **not an enabled encrypted mailbox**. Existing signing identities
and TLS providers are unchanged. No normal recipient key has been provisioned.

## Source and exact seam

BC Java 1.86, commit `eacda831fbe15b272933e40841d8b0827b556fae`, and
upstream main `6d7d611b9dcb20c59b2172975ba2a863f17705dc` were inspected.
Neither provided an external/private-DH callback suitable for the opaque handle.
The package-private KEM abstraction is not itself a usable external-DH adapter.

The reproducible patch changes only `DHKEM.Decap` and recipient-mode guards in
`HPKE`. One new `ExternalP256PrivateKey` capability holds a validated public key
and agreement operation, never a scalar. Normal private parameters retain the
stock path. External recipients support only Base/P-256; other modes fail.
Delta: two upstream files, 35 added/2 removed lines, plus the 48-line new
capability class (three source files, 83 added/2 removed lines total).
The DH callback receives validated canonical uncompressed P-256 bytes and
transfers an exact fresh 32-byte big-endian secret to DHKEM. BC's existing
ExtractAndExpand consumes it and the patched boundary clears it in finally.
No HPKE schedule, KDF, nonce, sequence, RNG, AEAD or ciphertext code is replaced.

BC validates the encapsulated point before the private callback. The capability
also checks its P-256 domain and recipient public binding. Malformed, infinity,
off-curve, wrong-length and alternate encodings cannot invoke private DH.
Platform adapters remain responsible for current key role/version/revocation.

## Reproducible dependency

`third_party/bc-hpke/source-pins.json` hashes every pristine source and license.
`scripts/build-bc-hpke-hook.ps1 -ProvisionSources` provisions them explicitly;
subsequent runs use verified cache only. A mechanical package relocation to
`com.peerchat.crypto.bc186.hpke` prevents replacement of signed stock BC jars
or changes to the existing TLS provider. The separate Java-8 artifact includes
the original license, source pins and patch. Fixed ZIP dates make repeated builds
with the tested JDK 22 produce the same artifact:

`4939210CDE012EFC343C72351B82A37F1C51E301A7BA5DDDAF2FF39BF329F62C`

Android's separate dependency script verifies this exact hash before use and
fails if it is missing/damaged. It never downloads during a normal build.
No jars, generated classes, host proof DLLs or private material are committed.

## Integration and evidence

- Android: `OpaqueRecipientHpke` delegates to a fresh upstream Base context;
  `AndroidRecipientHpke` uses the existing agreement-only Keystore provider.
  It opens an existing alias, checks the expected public binding before DH, and
  never creates or exports a key. Its caller must retain the KeyManager operation
  authorization/lease; it is not yet wired to a production registry or mailbox.
- Windows: test-only JNI bridges Java BC to the existing opaque CNG provider.
  100 stock-BC sender envelopes decrypt with real CNG keys; private export and
  signing are denied by the provider tests. Only namespaced test containers are
  created/deleted. **This host harness is not the shipped Windows runtime.**
- RFC A.3.1 differential normal/external decapsulation, zeroized DH, 100
  stock-to-hook and 100 hook-to-stock envelopes pass. Ciphertext/AAD tampering,
  malformed points, provider failure, stale handle, short result, public mismatch
  and unsupported modes fail closed.
- The pinned original upstream HPKE/KEM suite passes all 15 tests for stock and
  all 15 for the patched artifact, including normal-key paths and full vectors.
- Android JVM adapter test passes 100 envelopes with a synthetic fixture, binding
  mismatch, revoked capability, malformed/bounded input and tag failures. The
  complete JVM suite passes 69 tests; debug/test/release builds and lint pass.
- Real stock-sender to Keystore-recipient test is compiled (100 envelopes,
  repeated reopen, getEncoded unavailable, missing-key rejection, test-only
  cleanup). It has **not run on a physical device**. Actual Windows/Android
  cross-device HPKE proof also remains pending.

The execution safety reviewer accepted the narrow patch/test operations after
explicit review requests distinguishing it from the rejected custom HPKE stack.
That is not an independent cryptographic certification or physical acceptance.
The previously rejected native HPKE implementation remains unapplied.

Existing Windows Storage/Profile/identity/DPAPI/history/outbox/ACK/dedupe and
signed group-envelope regressions pass. Stock-BC original-message signing,
native/JNI TLS, mutual TLS 1.2/1.3 and unchanged Schannel interoperability pass.
The full Windows build and package/import audit pass at `4.0.2-dev.9`.
Android debug/test/release builds, 69 JVM tests, lint, ARM64/TLS audit and the
separate HPKE provenance/test-exclusion audit pass. The APK reports the same
`4.0.2-dev.9` version. Snapshots reside under that immutable development folder;
prior builds are not replaced. Debug APK SHA-256:
`503B232C94D07F8C6D7E31CCCC3D7A417824CBBADA2CCE3E8C90ACCA819D3133`.
These builds include preserved pre-existing uncommitted push/bootstrap work;
the foundation commits deliberately exclude it. ADB lists no physical device.

## Architectural blocker before production mailbox

The shipped Windows product is MinGW native Core plus .NET Framework WPF and
Schannel. It has no JVM/JNI runtime. Java BC/CNG success in a host test does not
make Java BC available in that production path. Official BC.NET source at
`4ccf1e6d2785aa8c3745723c21a4aa027eace7df` was checked: its complete tree has no
HPKE implementation. A hand-written translation would violate the no-custom-HPKE
boundary. Adding a JVM/Java-to-CLR deployment runtime, or selecting another
mature Windows HPKE library and reviewing its opaque-DH seam, is a material
runtime/dependency decision not silently included here.

Therefore the full security gate is **not resolved**. No production key binding,
mailbox server activation or delivery-state changes are made on the strength of
these host proofs. Phone absence is not the only remaining gate.

Sources: [BC Java pinned source](https://github.com/bcgit/bc-java/tree/eacda831fbe15b272933e40841d8b0827b556fae/core/src/main/java/org/bouncycastle/crypto/hpke),
[RFC 9180 vectors](https://www.rfc-editor.org/rfc/rfc9180.html#appendix-A.3.1),
[BC.NET inspected tree](https://github.com/bcgit/bc-csharp/tree/4ccf1e6d2785aa8c3745723c21a4aa027eace7df).
