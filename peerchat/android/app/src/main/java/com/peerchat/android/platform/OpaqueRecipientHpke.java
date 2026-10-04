package com.peerchat.android.platform;

import com.peerchat.crypto.bc186.hpke.ExternalP256PrivateKey;
import com.peerchat.crypto.bc186.hpke.HPKE;
import org.bouncycastle.crypto.AsymmetricCipherKeyPair;
import org.bouncycastle.crypto.InvalidCipherTextException;
import org.bouncycastle.crypto.params.ECPublicKeyParameters;

/** Library adapter only: no key creation, registry, message or retry ownership.
 * The caller supplies a currently authorized, role/version-checked capability.
 * One fresh upstream Base context per envelope. All HPKE algorithms stay in BC.
 */
public final class OpaqueRecipientHpke {
    public interface Recipient {
        // Must fail closed if the authorized handle is missing/revoked/replaced.
        byte[] publicKey();
        byte[] agree(byte[] validatedPoint);
    }
    private OpaqueRecipientHpke() {}
    public static byte[] open(Recipient recipient, byte[] expectedPublicKey,
            byte[] enc, byte[] info, byte[] aad, byte[] ciphertext)
            throws InvalidCipherTextException {
        if (recipient == null || info == null || aad == null || ciphertext == null
                || info.length > 65536 || aad.length > 65536
                || ciphertext.length < 16 || ciphertext.length > 1048576)
            throw new IllegalArgumentException("Invalid bounded HPKE input");
        final byte[] expected = RecipientPublicKey.validate(expectedPublicKey);
        final byte[] ephemeral = RecipientPublicKey.validate(enc);
        requireSameKey(recipient, expected);
        HPKE hpke = new HPKE(HPKE.mode_base, HPKE.kem_P256_SHA256,
                HPKE.kdf_HKDF_SHA256, HPKE.aead_AES_GCM128);
        ECPublicKeyParameters publicKey = (ECPublicKeyParameters)hpke.deserializePublicKey(expected);
        ExternalP256PrivateKey privateKey = new ExternalP256PrivateKey(publicKey, point -> {
            requireSameKey(recipient, expected);
            return recipient.agree(point); // BC takes ownership and clears DH.
        });
        return hpke.setupBaseR(ephemeral, new AsymmetricCipherKeyPair(publicKey, privateKey), info)
                .open(aad, ciphertext);
    }
    private static void requireSameKey(Recipient recipient, byte[] expected) {
        if (!org.bouncycastle.util.Arrays.constantTimeAreEqual(expected,
                RecipientPublicKey.validate(recipient.publicKey())))
            throw new IllegalStateException("Recipient capability changed");
    }
}
