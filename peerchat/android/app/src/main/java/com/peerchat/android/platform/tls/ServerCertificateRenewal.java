package com.peerchat.android.platform.tls;

import java.io.ByteArrayInputStream;
import java.security.MessageDigest;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import org.bouncycastle.asn1.ASN1OctetString;

/** A renewed leaf carries the original, publicly pinned certificate. The old
 * certificate is only a public-key trust anchor: its expired validity is never
 * used to authorize an expired leaf. TLS still proves possession of that key.
 * No network lookup, mutable global trust, or automatic key replacement. */
public final class ServerCertificateRenewal {
    public static final String ANCHOR_OID = "2.25.220970347019663391330263105371358931173";
    private ServerCertificateRenewal() {}
    private static final class RenewalProvider {
        static final java.security.Provider VALUE = new org.bouncycastle.jce.provider.BouncyCastleProvider();
    }
    public static boolean matches(X509Certificate leaf, byte[] expectedPin) {
        try {
            leaf.checkValidity();
            if (expectedPin == null || expectedPin.length != 32) return false;
            byte[] extension = leaf.getExtensionValue(ANCHOR_OID);
            if (extension == null || extension.length > 16384) return false;
            byte[] encoded = ASN1OctetString.getInstance(extension).getOctets();
            if (!MessageDigest.isEqual(expectedPin, MessageDigest.getInstance("SHA-256").digest(encoded))) return false;
            ByteArrayInputStream input = new ByteArrayInputStream(encoded);
            X509Certificate anchor = (X509Certificate)CertificateFactory.getInstance("X.509", RenewalProvider.VALUE).generateCertificate(input);
            if (input.available() != 0) return false;
            if (!MessageDigest.isEqual(anchor.getPublicKey().getEncoded(), leaf.getPublicKey().getEncoded())) return false;
            String algorithm = leaf.getSigAlgOID();
            if (!algorithm.equals("1.2.840.113549.1.1.11") && !algorithm.equals("1.2.840.113549.1.1.12") &&
                !algorithm.equals("1.2.840.113549.1.1.13") && !algorithm.equals("1.2.840.10045.4.3.2") &&
                !algorithm.equals("1.2.840.10045.4.3.3") && !algorithm.equals("1.2.840.10045.4.3.4")) return false;
            leaf.verify(anchor.getPublicKey(), RenewalProvider.VALUE);
            return true;
        } catch (Exception invalid) { return false; }
    }
}
