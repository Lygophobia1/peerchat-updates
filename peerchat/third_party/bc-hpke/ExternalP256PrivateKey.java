package org.bouncycastle.crypto.hpke;

import org.bouncycastle.crypto.ec.CustomNamedCurves;
import org.bouncycastle.crypto.params.AsymmetricKeyParameter;
import org.bouncycastle.crypto.params.ECDomainParameters;
import org.bouncycastle.crypto.params.ECPublicKeyParameters;
import org.bouncycastle.util.Arrays;

/** External P-256 recipient agreement capability. No scalar/export operation.
 * Base-mode DHKEM only; the provider owns key existence, role and version.
 * Callback input is validated canonical uncompressed P-256 public bytes.
 * Callback transfers a fresh, exact 32-byte big-endian DH result to DHKEM,
 * which clears it after its existing ExtractAndExpand completes.
 */
public final class ExternalP256PrivateKey extends AsymmetricKeyParameter
{
    public interface Agreement { byte[] agree(byte[] validatedPublicPoint); }
    private final ECPublicKeyParameters publicKey;
    private final Agreement agreement;
    private static final ECDomainParameters P256 = new ECDomainParameters(CustomNamedCurves.getByName("P-256"));

    public ExternalP256PrivateKey(ECPublicKeyParameters publicKey, Agreement agreement)
    {
        super(true);
        if (publicKey == null || agreement == null || !P256.equals(publicKey.getParameters()))
        {
            throw new IllegalArgumentException("external recipient requires validated P-256 key and agreement");
        }
        this.publicKey = publicKey;
        this.agreement = agreement;
    }

    byte[] agree(ECPublicKeyParameters remote, ECPublicKeyParameters recipient)
    {
        if (!P256.equals(remote.getParameters()) || !P256.equals(recipient.getParameters())
            || !publicKey.getQ().equals(recipient.getQ()))
        {
            throw new IllegalArgumentException("external recipient public key mismatch");
        }
        byte[] secret = agreement.agree(remote.getQ().getEncoded(false));
        if (secret == null || secret.length != 32)
        {
            if (secret != null) { Arrays.fill(secret, (byte)0); }
            throw new IllegalArgumentException("invalid external P-256 agreement result");
        }
        return secret;
    }
}
