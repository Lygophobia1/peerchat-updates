# Android opaque-identity peer TLS

Scope: TLS provider only. No discovery, login, socket owner, relay adapter or
messaging engine is introduced. Windows keeps `tls.cpp` / Schannel unchanged.
Phone validation remains pending until explicitly run.

## Pinned dependency

Low-level Bouncy Castle Java TLS, not a registered JSSE/JCA replacement provider:
`org.bouncycastle:bctls-jdk18on:1.86.1`; supporting `bcprov`, `bcutil`, `bcpkix`
`jdk18on:1.86`. Official distribution: https://www.bouncycastle.org/download/bouncy-castle-java/.
Sources and artifacts are published under `https://repo.maven.apache.org/maven2/org/bouncycastle/`.
No floating versions. Strict Gradle checksum verification covers every BC artifact.
Existing Android build-tool BC 1.77 artifacts are separately pinned tooling-only;
they are not application runtime dependencies. Other preexisting Android artifacts
retain their existing policy; this is not a claim of whole-SDK provenance verification.

| Artifact | Binary SHA-256 | Sources SHA-256 |
|---|---|---|
| bcprov-jdk18on-1.86 | 2af190b300cbb0b35e248ccf5f4a06b6072030aeb3da7a98ec73abe5b4cb371f | 34a8b7dcb030a08ddd5356d3d5be962f9299ca5275e1460273186f8fdbe44c34 |
| bcutil-jdk18on-1.86 | 1c268e15f785aafb1e4670d8d6e5f856f69475e96c162dadcd1ff9b350e9139f | a79e8cdbc04a24aecf64503eda9a4aa61019132484c64f7ce32a68ac30ef6df7 |
| bcpkix-jdk18on-1.86 | 8d8b41a4b149bdae8d331f059a578870954d1e3ff29f8269a9eb3ee19a7e4ef7 | 0bb00b50a80404e40c6465cb8012d3835d2a9100d94d16eaa9efed4587d7e34d |
| bctls-jdk18on-1.86.1 | 5ac3270a777fc6ebbfa38be8ffba24b1b45bb0aafefe1213a5771fb7eb5ecae5 | 509ecbd4fbd6535461d8109ed60bbd01a310fc14917fcfc53ad49225b3a8d1d6 |

License: MIT, copyright 2000–2026 The Legion of the Bouncy Castle Inc.
The upstream `META-INF/LICENSE.md` is retained in the APK. Redundant Java/OSGi
versioned manifests are excluded, not license notices. No native TLS library or
additional native dynamic dependency is introduced; these four artifacts are Java.
Local ignored host-proof cache: `build/deps/bouncycastle-1.86.1-jdk18on`.
Gradle resolves the same exact checksummed modules for Android.

## Callback byte semantics: source proof before integration

Pinned `bctls` sources, `org/bouncycastle/tls/TlsUtils.java`:

- TLS1.2 client CertificateVerify, line 2420: `handshakeHash.copyBufferTo`
  writes the original handshake transcript to the stream signer.
- TLS1.2 server ECDHE signature, lines 2394/2646: `sendSignatureInput` writes
  client random + server random + serialized key-exchange parameters.
- TLS1.3 CertificateVerify, lines 2466/2617: 64 spaces + role-specific context
  + NUL + transcript hash is the **original CertificateVerify signature input**.
  SHA256withECDSA hashes this complete structured input once, as required by TLS.
- Separate `generateRawSignature` branches (2435/2481/2652) receive a digest.
  `OpaqueTlsSigner` always rejects that API. It never forwards a digest to
  Android SHA256withECDSA and never falls back to raw-digest signing.

Host tests inspect actual TLS1.3 callback bytes, verify original-message signatures,
reject deliberate double hashing, and complete TLS1.2 mutual authentication with
unchanged Windows Schannel. This proves source/API semantics, not physical Keystore
execution. Original messages, raw64 signatures and CNG72 public keys preserve the
existing native identity representation. DER conversion is encoding only.

## Ownership and trust

`native Profile -> TlsCredentials -> TlsSession -> opaque byte-feed provider ->
PeerTls -> AndroidTlsIdentity -> existing AndroidIdentityProvider -> Keystore`.

Provider signing operations expose no private-key object or bytes. It reuses the
profile's existing SHA256-only P256 identity; no new identity is created by TLS.
No process-global provider registration, transport scheduler or Kotlin protocol.
Native instance gate serializes JNI calls. Buffers are copied/bounded at 256 KiB;
signing input is bounded at 70,000 bytes. A native profile admits at most 32 TLS
providers, retires their monotonic handles on errors/close, and destroys them before
releasing its storage/identity context. TLS ready means cryptographic peer identity
only, **not** friend or GroupControl authorization. Existing native session/delivery
owners must still authorize application dispatch when networking is ported.

Production selects TLS1.2 ECDHE-ECDSA/AES128-GCM, matching Schannel. Host-only
harness also proves the library TLS1.3 P256/SHA256 path. Native factories cannot
override certificates or turn off verification.

PeerChat peer certificates are self-signed device identities, not public DNS
server certificates: exactly one certificate, ECDSA/SHA256, canonical P256 key,
valid time interval, valid self-signature, and exact SHA256(CNG72) device pin are
required. Wrong/malformed/tampered/expired/not-yet-valid certificates fail closed.
This does **not** implement discovery-server CA-chain/hostname validation; that is
a separate future server-TLS boundary. No peer trust rule is reused as server trust.

## Reproducible validation

`./tests/test-peer-tls-host.ps1`: checks binary pins, compiles the original-message
adapter and real TLS harness; 1,000 signatures, 100 concurrent signatures, 100 mutual
TLS lifecycles, TLS1.2/1.3 fragmented records, bidirectional synthetic bytes, invalid
pin/certificate/time/provider rejection, and Windows Schannel interoperability.
Test-only ephemeral host keys remain outside APK/source state. Public compatibility
proof only is written under ignored build tests; no private fixture is persisted.

`./tests/test-profile-jni-host.ps1`: JNI lifecycle/provider containment regression.
`./tests/test-android-profile-artifacts.ps1`: ARM64 imports and release hook audit.
`PeerTlsTest#existingIdentityNativeMutualTls`: compiled physical-device test, 100
native TLS lifecycles reusing the ONE normal identity; no extra app/alias created.
Must not be reported as passing until actually executed on the phone.

Android build: `android/gradlew.bat -p android :app:assembleDebug
:app:assembleDebugAndroidTest :app:assembleRelease --console=plain` with existing
JDK22/SDK35/NDK27 toolchain. No WebRTC/media rebuild is required.
