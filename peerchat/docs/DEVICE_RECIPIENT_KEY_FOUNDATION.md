# Device recipient-key foundation: validation and remaining security gate

Date: 2026-10-04. Branch: `codex/windows-android-unified`.
This is a foundation checkpoint, **not an implemented encrypted mailbox**.
The accepted key audit remains the architecture reference.

## Implemented boundaries

- `DeviceKeyManager` borrows the existing identity provider (original-message
  signing/public-key operations) and a separate agreement-only recipient provider.
  It contains no private-key bytes, HPKE protocol, message owner, or retry owner.
- Recipient metadata has explicit role, stable ID/alias, algorithm, generation,
  creation time, provisioning/active/retired/revoked state, predecessor and server
  registration/binding fields. Rotation/registration protocols are not wired yet.
- Provisioning saves an intent before creating a platform key. Reopening a saved
  intent reuses that exact alias. Missing initialized keys, changed public keys,
  revoked keys and an orphaned requested alias after registry loss fail closed.
- `ProtectedKeyRegistry` has a bounded versioned codec, uses the existing
  `RecordProtector`, and binds protection to profile/domain. No scalar, private
  key, shared secret or content key field exists. It requires a platform adapter
  with cross-process serialization and atomic durable replace. The adapter is
  not yet connected to production Profile/Storage; the tests exercise the codec,
  real Windows DPAPI, and a controlled in-memory atomic-store fixture.
- Windows recipient provider uses persisted CNG ECDH-P256 containers, export
  policy zero, agreement-only usage, public SEC1 encoding and opaque agreement.
  CNG raw DH is little endian and is reversed to RFC big endian. Provider open
  never creates. Tests create only explicitly namespaced temporary containers
  and delete those exact test containers afterward.
- Android provider uses a distinct profile/generation alias, P-256,
  `PURPOSE_AGREE_KEY`, non-exportable Keystore handle and original signing key
  untouched. API 26-30 report unsupported; there is no exportable fallback or
  minSdk change. Open rejects missing keys, wrong alias/curve/purpose or an
  authentication-required key unsuitable for locked background delivery.
- Incoming Java public points are validated by pinned BC curve operations,
  requiring exactly canonical 65-byte uncompressed P-256 and rejecting infinity,
  out-of-field/off-curve/alternate forms. CNG import/agreement also rejects the
  malformed/off-curve inputs in its test set.

These adapters are not invoked to create a normal production recipient key yet.
No existing signing identity, profile, Remember Me state or device was reset.

## What the standards test proves (and does not prove)

`tests/test-device-keys.ps1` checks the exact existing bcprov 1.86 SHA-256:
`2AF190B300CBB0B35E248CCF5F4A06B6072030AEB3DA7A98EC73ABE5B4CB371F`.

The unmodified library matches RFC 9180 Appendix A.3.1 for suite
`0x0010/0x0001/0x0001`: derived public/private **published test** values, sender
encapsulation, ciphertext sequences 0/1/2, and all three exporter examples.
Tampered ciphertext/AAD and malformed point cases reject. Published deterministic
scalars exist only in the host test, not the application.

An independent BC ECDH operation matches a newly generated opaque CNG handle.
The transient comparison values are not printed. This validates agreement and
byte-order normalization; **it is not opaque HPKE decapsulation or complete
cross-platform HPKE interoperability**. Key/base-nonce/exporter-secret internals
are not exposed by the standard test and are not claimed separately validated.

Sources:

- https://www.rfc-editor.org/rfc/rfc9180.html#appendix-A.3.1
- https://learn.microsoft.com/en-us/windows/win32/api/ncrypt/nf-ncrypt-ncryptderivekey
- https://developer.android.com/reference/android/security/keystore/KeyProperties#PURPOSE_AGREE_KEY

## Explicit stop boundary

The attempted native RFC glue was rejected by the execution safety reviewer as
too broad (key schedule, encapsulation, context/nonce and AEAD orchestration),
without prior implementation-specific interoperability proof. That patch was
**not applied**; `portable/recipient_hpke.hpp` does not exist. No workaround or
private-key export was used.

The pinned BC KEM still consumes scalar-backed ECPrivateKeyParameters rather
than an opaque platform key. Standard-library vector success does not fix that
integration boundary. The next security review should cover a genuinely narrow
external-DH hook that leaves mature DHKEM extraction, HPKE schedule and AEAD in
the reviewed implementation. Both platform recipients and the shared sender must
then pass the hard interoperability gate before production registration/mailbox.

The Motorola was not listed by ADB in this pass. No install, clear, uninstall,
identity replacement or physical test occurred. Phone availability is a separate
gate, **not the only remaining mailbox blocker**.

## Validation completed

- KeyManager: 1,000 reopens; interruption after key creation/before metadata
  replacement; missing registry/key; corruption; revoked/retired state; mismatch;
  capability rejection; unchanged identity operations.
- Protected registry: 1,000 actual DPAPI authenticated reads; wrong profile;
  tamper/truncation/empty/corrupt version; every truncated codec prefix; trailing
  bytes, duplicates, provider failure and accepted-without-binding rejection.
- CNG: agreement symmetry; 100 persisted reopens; private export denied; signing
  denied; duplicate create denied; malformed/off-curve inputs; BC interoperability.
- Windows existing portable Storage set: identity/CNG, DPAPI, protected records,
  Profile reopens, history/outbox/ACK/dedupe, transaction rollback, malformed DB,
  opaque provider behavior and signed AES-GCM group envelopes passed.
- Existing peer TLS: original-message signatures, 1,000 repeats/100 concurrent
  signs, JNI lifecycle, mutual TLS 1.2/1.3, certificate/pin/date/provider failures
  and unchanged Schannel interoperability passed.
- Native/JNI reliable profile/server test: three remembered-bootstrap/profile
  reopen cycles, six bidirectional deliveries, two offline queued IDs drained
  once, ACK/dedupe, superseded presence and nonfriend rejection passed. Expected
  closed-socket warnings occur in deliberate cancellation/failure fixtures.
- Android: 68 JVM tests; debug/test/release builds; lint; ARM64/JNI/dependency
  audits passed. Keystore recipient device test compiled but was not run.
- Full Windows build/package/import audit passed: `4.0.2-dev.8`.

## Requested final-report coverage

| Item | Status |
|---|---|
| 1 KeyManager | Tested opaque foundation; not production Profile wiring |
| 2 Registry | Tested protected codec/store boundary; durable platform attachment pending |
| 3 Android recipient key | Compiled agreement-only adapter/device test; physical proof pending |
| 4 Windows recipient key | Actual non-exportable CNG adapter and host proof passed |
| 5 HPKE approach | RFC suite fixed; narrow reviewed external-DH integration pending |
| 6 Custom crypto glue | No HPKE glue applied; provider encoding/opaque DH adaptation only |
| 7 RFC vectors | Unmodified pinned BC vectors passed, as scoped above |
| 8 Cross-implementation | BC/CNG raw agreement passed; full opaque HPKE not proven |
| 9 Signed binding | Not implemented; cannot enable before HPKE gate |
| 10 Server registration | Not implemented |
| 11 Mailbox protocol | Design only; not enabled |
| 12 Server schema/storage | No mailbox payload schema/storage added |
| 13 Delivery states | Existing semantics unchanged; no Stored transition added |
| 14 Live/mailbox selection | Existing live delivery preserved; mailbox unavailable |
| 15 No-server-storage | Existing behavior preserved; new policy not implemented |
| 16 FCM mailbox wake | Existing opaque FCM preserved; no mailbox wake integration |
| 17 Sender offline | Existing durable local queue/composer checkpoint preserved |
| 18 Background/locked | No fresh physical result; no mailbox acceptance claim |
| 19 Server restart | Existing tests only; mailbox restart not implemented/tested |
| 20 Client restart | Existing native/profile persistence passed; mailbox path pending |
| 21 Idempotence/dedupe | Existing delivery tests passed; mailbox-specific tests pending |
| 22 TTL/quota/revocation | Local metadata rejection tested; server mailbox controls pending |
| 23 Provider optimization | Existing scoped reuse preserved and regression passed |
| 24 Further optimization | Not attempted; mailbox security remains priority |
| 25 Physical | None this pass; ADB device absent |
| 26 Build/tests | Results above; no production mailbox validation |
| 27 Checkpoints | Foundation/registry plus platform/test checkpoints; see Git log |
| 28 Blockers | Reviewed opaque HPKE integration, production wiring, then physical validation |

No main merge. Unrelated unified push/bootstrap changes and generated objects
remain excluded from the new key-foundation commits.
