using System;
using System.IO;
using System.Linq;
using PeerChat.Server;
internal static class DeviceIdentityEndorsementServerTest {
    static void Check(bool ok){if(!ok)throw new Exception("Endorsement interop assertion failed");}
    static void Reject(Action action){bool failed=false;try{action();}catch(InvalidOperationException){failed=true;}Check(failed);}
    static int Main(string[] args){try{
        var wire=File.ReadAllBytes(args[0]);var e=DeviceIdentityEndorsement.Parse(wire);
        Func<string,long,long,bool> match=(server,revision,now)=>e.MatchesChallenge(server,e.Target.Account,e.Authority,e.AuthorityKey,
            e.Target.Device,e.Target.Public,e.AuthorityGeneration,e.TargetGeneration,revision,e.Issuance,e.Session,e.Issued,e.Expires,now);
        Check(match(e.Target.Server,e.Revision,e.Issued));Check(!match(e.Target.Server,e.Revision,e.Expires));
        Check(!match(new string('f',64),e.Revision,e.Issued));Check(!match(e.Target.Server,e.Revision+1,e.Issued));
        for(int i=0;i<wire.Length;i++){int length=i;Reject(()=>DeviceIdentityEndorsement.Parse(wire.Take(length).ToArray()));}
        var bad=(byte[])wire.Clone();bad[bad.Length-1]^=1;Reject(()=>DeviceIdentityEndorsement.Parse(bad));
        Reject(()=>DeviceIdentityEndorsement.Parse(wire.Concat(new byte[]{0}).ToArray()));
        Check(!e.MatchesChallenge(e.Target.Server,e.Target.Account+1,e.Authority,e.AuthorityKey,e.Target.Device,e.Target.Public,
            e.AuthorityGeneration,e.TargetGeneration,e.Revision,e.Issuance,e.Session,e.Issued,e.Expires,e.Issued));
        Console.WriteLine("PASS native ECDSA endorsement -> portable server parser; canonical bounds, tampering, account/server/revision and challenge expiry. Parser only; no enrollment admission claim.");return 0;
    }catch(Exception e){Console.Error.WriteLine(e.GetType().Name+": "+e.Message);return 1;}}
}
