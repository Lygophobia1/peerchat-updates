> HISTORICAL ONLY — describes the dev.17 evaluation/proof, not the current provider or build instructions. The former provider, evaluation switch and dependency have been removed. See GO_HPKE_PRODUCTION_INTEGRATION.md for the current Go integration.

# Windows native opaque-recipient HPKE evaluation

Status: **evaluation only, not approved for production**. Date: 2026-10-04.
Starting HEAD: `2423c4c` on `codex/windows-android-unified`.
BC checkpoints `cacb3cc`, `98e5a52`, `91abbdc`, `2423c4c` remain intact.
No production mailbox key, recipient registration, mailbox queue, Windows JVM,
Android provider change, or main merge was introduced during this evaluation.

Required suite throughout: RFC 9180 Base, KEM `0x0010` P-256/HKDF-SHA256,
KDF `0x0001` HKDF-SHA256, AEAD `0x0001` AES-128-GCM.

## Candidate comparison

| Candidate inspected | License | Native HPKE / opaque-recipient result |
| --- | --- | --- |
| OpenSSL 3.5.8, `f4dc4d58b48d346a8270183f89acf826d459b0ca` | Apache-2.0 | Suite supported. Unmodified default DHKEM cannot consume a CNG-only key through an ECDH-only provider. |
| BoringSSL `dd73e69a4e86fa178a4d19033c691e9b42cc1088` | Mixed permissive notices; preserve upstream LICENSE | Suite supported, but its P-256 HPKE key is raw-scalar-backed, not opaque. No supported P-256 external-DH seam at this revision. |
| Botan 3.13.0, `2a81eef56c96c237e590c27f9a75e60317c9c700` | BSD-2-Clause | External key providers exist, but no HPKE implementation was found in this release's source tree. Not a complete candidate. |
| rust-hpke v0.14.0, `9c2e9bbda684414c3d6c760838b46adf48c71ee5` | MIT / Apache-2.0 | HPKE exists; DH interface is internal and its private-key type requires serialization/deserialization. No unmodified public opaque-provider integration. |
| hpke-rs v0.6.1, `f3463e7530771d7f7116635335c25e7d2d11e861` | MIT / Apache-2.0 | Provider DH/secret-validation/public-derivation interfaces consume private-secret byte slices. No typed opaque key integration at the inspected tag. |
| wolfCrypt in wolfSSL v5.9.4-stable, `3c5eead44904df64e6a5a1f4ebdce377d35a849a` | GPLv3 or commercial | Technically viable using existing crypto callbacks and unchanged upstream HPKE. Host proof passes; licensing and physical Android gate remain unresolved. |

OpenSSL was inspected first. `OSSL_HPKE_decap` uses
`EVP_PKEY_decapsulate`, with the DHKEM operation. The default
`providers/implementations/kem/ec_kem.c` checks an actual private EC scalar
and calls internal `ECDH_compute_key`, not `EVP_PKEY_derive` through an
external key-exchange provider. Supplying only a CNG ECDH key-exchange provider
does not satisfy that default KEM. A replacement KEM provider would have to
implement DHKEM, outside the approved glue boundary. An upstream redesign could
change this, but none was implemented. No raw-key fallback was attempted.

BoringSSL's `crypto/hpke/hpke.cc` P-256 initialization copies a 32-byte scalar;
decapsulation uses that scalar directly. At the inspected commit its P-256 HPKE
descriptor has null EVP_PKEY/EVP_KEM hooks and a TODO to expose P-256 as EVP_KEM.
Other KEM migration work is not evidence of an opaque P-256 recipient API.
BoringSSL also does not promise third-party API/ABI stability. Patching its key
representation and every copy/decapsulation caller is a larger fork than the
existing wolfCrypt callback, not a demonstrated production solution.

Rejected candidates were source-evaluated, not built or packaged. Custom glue
size is **zero implemented** for them; no invented line-count estimates are
offered for the unimplemented provider/fork work.

## wolfCrypt pin and build

Official archive:
`https://codeload.github.com/wolfSSL/wolfssl/zip/3c5eead44904df64e6a5a1f4ebdce377d35a849a`

Archive SHA-256:
`C20D50CE143CBF2E8DBBD91E1035C75E99112DB7774D399E969894BE036367A7`.

The test entry point compares **every extracted upstream file** against the
checksum-pinned archive. No upstream file was patched. This is not the wolfCrypt
FIPS module and makes no FIPS claim.

Evaluation cache: `build/deps/wolfcrypt-evaluation` (ignored, not distributed).
Compiler: TDM-GCC 10.3.0 x64. CMake 3.22.1/Ninja from the Android SDK tools.
Release/static build with HPKE, crypto callbacks, ECC, AES-GCM and HKDF;
assembly off; examples/tests off; source selection crypto-only.
`WOLFCRYPT_ONLY` and `NO_CERTS` must also be passed as C preprocessor defines:
the CMake source-selection option alone left certificate-manager references in
the first attempted archive. See the repeatable script for exact flags.

Measured evaluation archive: **743,514 bytes**. SHA-256:
`830B85BE3482A663F8F76A167268F497388A6B016F224F40D10AE80733D8E097`.
The archive contains additional default crypto algorithms; it is not a
production-minimized footprint. The C++ proof EXE before the final callback
guard edit was 3,565,370 bytes including static GCC/C++ runtime and symbols.
That is a harness size, **not** a measured increase in PeerChat.Core.dll.
No production binary size or Windows ARM64 build has been proven yet.

Imported DLLs in the evaluation EXE: `ADVAPI32.dll`, `KERNEL32.dll`,
`msvcrt.dll`, `ncrypt.dll`. No JVM, third-party runtime DLL, wolfSSL TLS
implementation, or PeerChat binary is shipped by this evaluation.

## Opaque CNG boundary and execution review

Upstream `wolfcrypt/src/hpke.c` calls `wc_ecc_shared_secret` from its existing
decapsulation implementation. `wc_HpkeCopyPrivateKey` explicitly skips private
export/copy for an ECC key with a device ID. The bridge imports only the
public point, retaining an `ECC_PUBLICKEY` object with a callback device ID.
It never populates a software private scalar or pretends the key is exportable.

`OpaqueBridge` is approximately 30 lines in the native evaluation harness;
it reuses the already-reviewed `WindowsRecipientKeyProvider`. It performs only
callback dispatch, public validation, CNG agreement and zeroization. It does not
implement DHKEM, HKDF, HPKE labels, scheduling, nonce derivation or AES-GCM.
Upstream HPKE remains authoritative for those operations.

Important review findings:

- `wc_ecc_shared_secret` dispatches the external callback before its ordinary
  software validation. The bridge checks P-256/domain/device/key ownership and
  explicitly calls upstream `wc_ecc_check_key` on the software public point
  **before** CNG agreement. Encoding is restricted to exact 65-byte uncompressed
  SEC1. Malformed, off-curve, out-of-field and wrong-sized input is rejected.
- CNG performs agreement through the existing non-exportable handle. Public-key
  export is allowed; private export is tested and denied. No PKCS#8/private blob
  is created. DH is a fresh 32-byte result, cleared immediately after copying
  into upstream's buffer, which upstream also clears after KEM processing.
- Provider failure returns `WC_HW_E`, not `CRYPTOCB_UNAVAILABLE`. A deliberately
  unregistered callback also fails: the key contains no software private scalar
  from which a fallback could calculate agreement.
- The evaluation bridge has single-owner, serial lifetime. RAII unregisters it
  before callback context/key destruction. A production adapter would need
  independently reviewed device-ID allocation, registry synchronization and
  concurrent-call/shutdown tests; the fixed test ID is not a production design.
- `wolfCrypt_Init` is required before RNG/mutex use. The initial test omission
  caused a harness failure and was corrected; cleanup is paired by RAII.
- Unlike BC, wolfCrypt's open API receives a body length **excluding** the
  appended 16-byte tag. Passing the full length caused a test-harness overwrite
  in early evaluation. The final harness validates total size before subtracting
  the tag. A production adapter must bound all lengths/capacities at this edge.
- Output is not accepted before AEAD success. Wrong recipient, ciphertext and
  AAD reject. Buffers are cleared on failure. The Java oracle has a 90-second
  watchdog, and the test uses only synthetic fixture content.
- Early debugger crashes left four exact `PeerChat.Recipient.test.wolf.*`
  containers. They were individually enumerated and removed. A subsequent
  successful run verifies no added test key remains. No normal key was deleted.

This is an internal execution review, not an external audit or blanket security
approval. Production binding/version/revocation policy, hostile-input fuzzing,
concurrency, Windows ARM64, minimized package footprint and physical Android
interoperability remain further gates.

## Automated evidence

Run from the unified checkout:

```powershell
.\tests\test-wolfcrypt-cng-evaluation.ps1
# Optional, explicit evaluation-library build, never invoked by build.ps1:
.\tests\test-wolfcrypt-cng-evaluation.ps1 -BuildEvaluationDependency
```

Inputs are cached and verified; no download or unrelated dependency build is
performed by the script. Java/BC is an independent test oracle only.

Passed:

- RFC 9180 Appendix A.3.1 deterministic sender: fixed `enc`/public key,
  `key`, `base_nonce`, `exporter_secret`, three ciphertexts.
- 100 stock unmodified BC senders -> Windows opaque CNG receiver.
- 100 Windows native senders -> stock BC receiver.
- 100 patched BC senders -> Windows opaque CNG receiver.
- 100 Windows native senders -> patched BC host external-DH receiver.
- Private CNG export denied; wrong device cannot decrypt.
- Ciphertext/AAD alteration rejected; malformed/unsupported/invalid public
  points rejected before private agreement; callback failure/missing callback
  rejected; restoring a valid callback succeeds.
- All upstream sources match the pinned archive; test-key cleanup verified.

The patched receiver above uses a synthetic host software key **behind the
external-DH interface**. It is not the physical Android Keystore provider.
The upstream HPKE shared-secret local is not exposed by the public API; no
custom crypto was added to extract it. Context values and ciphertexts are
compared directly against published vectors. Existing BC vector evidence is
preserved rather than replaced.

ADB reported **no connected devices** during this evaluation. Actual Windows
native <-> Android Keystore interoperability has **not** passed. No Android
APK or Windows product build was modified, and existing product regressions
were not reclassified as runs of this evaluation. `git diff --check` passes.

## Maintenance, licensing and decision

wolfSSL 5.9.4 was released September 25, 2026. Its source release notes include
security fixes; the vendor publishes a vulnerability list. Neither release
recency nor passing interoperability proves that all applicable CVEs are absent.
Before production use, map advisories to the exact compiled crypto options,
monitor the vendor security feed, pin reviewed updates, and rerun RFC/BC/CNG/
Android failure and interoperability tests on every update. Never silently float
versions. Previous HPKE/ECH advisories make hostile-length testing important.

The inspected source `LICENSING` permits GPLv3 or a commercial license. The
repository's ability/intention to distribute under GPLv3 was not established;
no commercial license was supplied. This cannot be silently resolved by linking
a static archive. Licensing must be approved before shipping/adoption.

**Recommendation:** wolfCrypt is the smallest demonstrated native technical
path: unchanged upstream HPKE plus an existing opaque-DH callback into CNG.
It is not internally approved for production yet, because suitable licensing,
physical Android interop and the remaining production integration/security
review are unresolved. Mailbox implementation did **not** continue.

Secure next options: authorize GPLv3 distribution or supply a suitable
commercial license and complete the remaining gates; otherwise pursue a
reviewed upstream opaque-key capability in a permissively licensed candidate.
Do not export CNG keys, add custom DHKEM, or add a Windows JVM as a fallback.

Primary sources:

- [OpenSSL HPKE API](https://docs.openssl.org/3.5/man3/OSSL_HPKE_CTX_new/)
- [OpenSSL inspected DHKEM source](https://github.com/openssl/openssl/blob/f4dc4d58b48d346a8270183f89acf826d459b0ca/providers/implementations/kem/ec_kem.c)
- [BoringSSL inspected HPKE source](https://github.com/google/boringssl/blob/dd73e69a4e86fa178a4d19033c691e9b42cc1088/crypto/hpke/hpke.cc)
- [wolfCrypt pinned HPKE source](https://github.com/wolfSSL/wolfssl/blob/3c5eead44904df64e6a5a1f4ebdce377d35a849a/wolfcrypt/src/hpke.c)
- [wolfSSL licensing](https://www.wolfssl.com/license/)
- [wolfSSL 5.9.4 release](https://www.wolfssl.com/wolfssl-5-9-4-release-blog/)
- [wolfSSL security advisories](https://www.wolfssl.com/docs/security-vulnerabilities/)
