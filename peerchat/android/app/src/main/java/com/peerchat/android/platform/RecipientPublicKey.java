package com.peerchat.android.platform;

import org.bouncycastle.asn1.x9.ECNamedCurveTable;
import org.bouncycastle.asn1.x9.X9ECParameters;
import org.bouncycastle.math.ec.ECPoint;
import java.security.AlgorithmParameters;
import java.security.KeyFactory;
import java.security.PublicKey;
import java.security.spec.ECGenParameterSpec;
import java.security.spec.ECParameterSpec;
import java.security.spec.ECPublicKeySpec;
import java.util.Arrays;

/** Canonical HPKE P-256 public validation delegated to pinned BC curve code. */
public final class RecipientPublicKey {
    private static final X9ECParameters P256 = ECNamedCurveTable.getByName("secp256r1");
    private RecipientPublicKey() {}
    public static byte[] validate(byte[] encoded) {
        if (encoded == null || encoded.length != 65 || encoded[0] != 4)
            throw new IllegalArgumentException("Invalid recipient public encoding");
        // BC validates field coordinates and the curve equation. P-256 has
        // cofactor one. Infinity and all alternate encodings are forbidden.
        ECPoint point = P256.getCurve().decodePoint(encoded).normalize();
        if (point.isInfinity() || !point.isValid() || !Arrays.equals(encoded, point.getEncoded(false)))
            throw new IllegalArgumentException("Invalid recipient public point");
        return encoded.clone();
    }
    public static PublicKey jca(byte[] encoded) throws Exception {
        byte[] canonical = validate(encoded);
        ECPoint point = P256.getCurve().decodePoint(canonical).normalize();
        AlgorithmParameters parameters = AlgorithmParameters.getInstance("EC");
        parameters.init(new ECGenParameterSpec("secp256r1"));
        return KeyFactory.getInstance("EC").generatePublic(new ECPublicKeySpec(
            new java.security.spec.ECPoint(point.getAffineXCoord().toBigInteger(), point.getAffineYCoord().toBigInteger()),
            parameters.getParameterSpec(ECParameterSpec.class)));
    }
}
