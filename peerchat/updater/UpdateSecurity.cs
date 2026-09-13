using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;

namespace PeerChat.Updater
{
    public static class PackageHashVerifier
    {
        public static bool VerifySha256(string path, string expectedHex)
        {
            if (!File.Exists(path)) return false;
            byte[] hash;
            using (FileStream input = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
            using (SHA256 algorithm = SHA256.Create()) hash = algorithm.ComputeHash(input);
            string actual = BitConverter.ToString(hash).Replace("-", "");
            return FixedTimeEquals(actual, expectedHex);
        }

        static bool FixedTimeEquals(string left, string right)
        {
            if (left == null || right == null) return false;
            left = left.ToUpperInvariant(); right = right.ToUpperInvariant();
            int difference = left.Length ^ right.Length;
            int length = Math.Min(left.Length, right.Length);
            for (int i = 0; i < length; ++i) difference |= left[i] ^ right[i];
            return difference == 0;
        }
    }

    public interface IReleaseSignatureVerifier
    {
        bool Verify(UpdateManifest manifest, string packagePath, out string failureReason);
    }

    public sealed class TrustedReleasePublicKey
    {
        public string KeyId { get; private set; }
        public string Algorithm { get; private set; }
        public string ModulusBase64 { get; private set; }
        public string ExponentBase64 { get; private set; }
        public bool Revoked { get; private set; }

        public TrustedReleasePublicKey(string keyId, string algorithm, string modulusBase64,
                                       string exponentBase64, bool revoked)
        {
            KeyId = keyId;
            Algorithm = algorithm;
            ModulusBase64 = modulusBase64;
            ExponentBase64 = exponentBase64;
            Revoked = revoked;
        }
    }

    public static class TrustedReleaseKeys
    {
        public const string RsaSha256Pkcs1Algorithm = "rsa-sha256-pkcs1-v1_5";

        // Public verification material only. Add a replacement key here in an update signed by an
        // already trusted key before the publisher begins using the replacement key ID.
        public static IList<TrustedReleasePublicKey> CreateDefaults()
        {
            return new List<TrustedReleasePublicKey> {
                new TrustedReleasePublicKey(
                    "peerchat-release-2026-01",
                    RsaSha256Pkcs1Algorithm,
                    "zvHGz9DpbzLFWB7zHBiUkGAceeaBSxUfImd8UNfwVIMUqxU1ZBXhsUrsX5ipD/WXw3otXrdWhgek1QrQ9ZTZo3BI84A1ucL9IogmmvggX38pRBdtQxkReaUa6V62s3GRl0oOUb8ioZdFylkr5DE8PSpd69Cnib8XvPDFQOUJ1KyLUpT6nig+WoO6GGQO7NEXPg8BhTbJoS76NA9iRMXZzssfAEY0fzsmDO39je+RXxlNuLCKIMYBmgpNAUcLQioWnf04eilj2qqxg+h3/DW9BGcqZVD14rympn5o4H2oMWfwWMq7mjviXbAHLUpY/Y5TldvsXtK0/4H01FOQmuUjii7LNuSNbIOv3VlM1LlSdLCzxywsUvn/9gopIm4ybZEHWPqaibuR49g/RMm8LIqmNDruVM3C5j8zTSM5QlH2JN91n7NyyRhIDcgZgUfaMeuDyI0miOXbZHd2bjI+iiR1QSJj6PDJBaXgY+SyYzwcONb7+F0bdVq2Rp5fe66i35PJ",
                    "AQAB",
                    false)
            };
        }
    }

    public sealed class SignatureBoundaryVerifier : IReleaseSignatureVerifier
    {
        readonly bool requireSignature;
        readonly IList<TrustedReleasePublicKey> trustedKeys;

        public SignatureBoundaryVerifier(bool requireSignature)
            : this(requireSignature, TrustedReleaseKeys.CreateDefaults()) { }

        public SignatureBoundaryVerifier(bool requireSignature, IList<TrustedReleasePublicKey> trustedKeys)
        {
            this.requireSignature = requireSignature;
            this.trustedKeys = trustedKeys ?? new List<TrustedReleasePublicKey>();
        }

        public bool Verify(UpdateManifest manifest, string packagePath, out string failureReason)
        {
            if (manifest == null)
            {
                failureReason = "The update manifest is missing.";
                return false;
            }
            if (manifest.Signature == null)
            {
                failureReason = requireSignature ? "The release is unsigned, but this update feed requires signatures." : null;
                return !requireSignature;
            }

            TrustedReleasePublicKey trusted = null;
            foreach (TrustedReleasePublicKey candidate in trustedKeys)
                if (String.Equals(candidate.KeyId, manifest.Signature.KeyId, StringComparison.Ordinal))
                { trusted = candidate; break; }
            if (trusted == null)
            {
                failureReason = "The release signature uses an unknown key ID.";
                return false;
            }
            if (trusted.Revoked)
            {
                failureReason = "The release signature uses a revoked key ID.";
                return false;
            }
            if (!String.Equals(manifest.Signature.Algorithm, trusted.Algorithm, StringComparison.Ordinal) ||
                !String.Equals(manifest.Signature.Algorithm, TrustedReleaseKeys.RsaSha256Pkcs1Algorithm, StringComparison.Ordinal))
            {
                failureReason = "The release signature algorithm is not trusted for this key.";
                return false;
            }
            if (String.IsNullOrWhiteSpace(packagePath) || !File.Exists(packagePath) ||
                new FileInfo(packagePath).Length != manifest.PackageSize ||
                !PackageHashVerifier.VerifySha256(packagePath, manifest.PackageSha256))
            {
                failureReason = "The signed release metadata does not match the downloaded package.";
                return false;
            }

            try
            {
                byte[] signature = Convert.FromBase64String(manifest.Signature.Value);
                RSAParameters parameters = new RSAParameters {
                    Modulus = Convert.FromBase64String(trusted.ModulusBase64),
                    Exponent = Convert.FromBase64String(trusted.ExponentBase64)
                };
                using (RSACryptoServiceProvider rsa = new RSACryptoServiceProvider())
                using (SHA256 sha256 = SHA256.Create())
                {
                    rsa.PersistKeyInCsp = false;
                    rsa.ImportParameters(parameters);
                    if (!rsa.VerifyData(manifest.GetSignaturePayload(), sha256, signature))
                    {
                        failureReason = "The release signature is invalid.";
                        return false;
                    }
                }
                failureReason = null;
                return true;
            }
            catch (FormatException)
            {
                failureReason = "The release signature or trusted public key is malformed.";
                return false;
            }
            catch (CryptographicException)
            {
                failureReason = "The release signature or trusted public key is malformed.";
                return false;
            }
        }
    }
}
