// Test-only key generation. Production device identities are never loaded here.
// Keep canonical Windows public-blob bytes and P1363 signatures in all fixtures.
using System;
using System.Security.Cryptography;

sealed class PortableTestKey : IDisposable {
    readonly ECDsa signer;
    internal PortableTestKey(int bits){if(bits!=256)throw new ArgumentException("P-256 only");signer=ECDsa.Create(ECCurve.NamedCurves.nistP256);}
    internal PortableTestKey Key { get { return this; } }
    internal byte[] Export(CngKeyBlobFormat format){
        if(format!=CngKeyBlobFormat.EccPublicBlob)throw new InvalidOperationException("Test public export only");
        var p=signer.ExportParameters(false);var result=new byte[72];
        result[0]=69;result[1]=67;result[2]=83;result[3]=49;result[4]=32;
        Buffer.BlockCopy(p.Q.X,0,result,8,32);Buffer.BlockCopy(p.Q.Y,0,result,40,32);return result;
    }
    internal byte[] SignHash(byte[] hash){return signer.SignHash(hash,DSASignatureFormat.IeeeP1363FixedFieldConcatenation);}
    internal byte[] SignData(byte[] bytes){return signer.SignData(bytes,HashAlgorithmName.SHA256,DSASignatureFormat.IeeeP1363FixedFieldConcatenation);}
    public void Dispose(){signer.Dispose();}
}
