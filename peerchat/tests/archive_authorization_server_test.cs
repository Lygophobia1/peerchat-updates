using System;
using System.IO;
using System.Linq;
using PeerChat.Server;

internal static class ArchiveAuthorizationServerTest {
    static void Check(bool value){if(!value)throw new Exception("Archive server metadata regression failed.");}
    static void Reject(Action operation){bool rejected=false;try{operation();}catch(InvalidOperationException){rejected=true;}Check(rejected);}
    static byte[] Read(string root,string name){return File.ReadAllBytes(Path.Combine(root,name+".bin"));}
    public static int Main(string[] args){try{
        string root=args[0],path=Path.Combine(root,"archive-metadata.sqlite");
        var owner=ArchiveAuthorizationProof.Descriptor.Parse(Read(root,"issuer-descriptor"));
        var reader=ArchiveAuthorizationProof.Descriptor.Parse(Read(root,"reader-descriptor"));
        var other=ArchiveAuthorizationProof.Descriptor.Parse(Read(root,"other-descriptor"));
        byte[] serve=Read(root,"serve"),read=Read(root,"read"),revoke=Read(root,"revoke"),reenroll=Read(root,"reenroll");
        Check(ArchiveAuthorizationProof.Grant.Parse(read,owner.Public).Device==reader.Device);
        var tampered=(byte[])read.Clone();tampered[tampered.Length-1]^=1;
        Reject(()=>ArchiveAuthorizationProof.Grant.Parse(tampered,owner.Public));
        Reject(()=>ArchiveAuthorizationProof.Grant.Parse(read.Take(read.Length-1).ToArray(),owner.Public));
        Reject(()=>ArchiveAuthorizationProof.Grant.Parse(read.Concat(new byte[]{0}).ToArray(),owner.Public));
        Reject(()=>ArchiveAuthorizationProof.Grant.Parse(new byte[2049],owner.Public));
        Reject(()=>ArchiveAuthorizationProof.Grant.Parse(read,reader.Public));
        using(var db=new AccountDatabase(path)){
            db.Execute("INSERT INTO accounts(id,username,password_salt,password_hash,created_utc) VALUES(8,'archive-test',X'00',X'00',1),(9,'other-account',X'00',X'00',1);");
            foreach(string device in new[]{owner.Device,reader.Device,other.Device})Check(db.AddDevice(8,device));
            Check(!db.ArchiveAuthorized(8,owner.Device,reader.Device,1,1));
            Check(db.RegisterArchiveGrant(8,owner.Device,read)=="issuer_descriptor_required");
            Check(db.RegisterArchiveDescriptor(9,owner.Device,owner.Wire)=="not_authorized");
            Check(db.RegisterArchiveDescriptor(8,reader.Device,owner.Wire)=="not_authorized");
            Check(db.RegisterArchiveDescriptor(8,owner.Device,owner.Wire)=="accepted");
            Check(db.RegisterArchiveDescriptor(8,owner.Device,owner.Wire)=="accepted");
            Check(db.RegisterArchiveGrant(8,owner.Device,read)=="target_not_authorized");
            Check(db.RegisterArchiveDescriptor(8,reader.Device,reader.Wire)=="accepted");
            Check(db.RegisterArchiveDescriptor(8,other.Device,other.Wire)=="accepted");
            Check(db.RegisterArchiveGrant(8,owner.Device,Read(root,"foreign"))=="not_authorized");
            Check(db.RegisterArchiveGrant(9,owner.Device,read)=="not_authorized");
            Check(db.RegisterArchiveGrant(8,owner.Device,serve)=="accepted");
            Check(db.RegisterArchiveGrant(8,owner.Device,read)=="accepted");
            Check(db.RegisterArchiveGrant(8,owner.Device,read)=="accepted");
            Check(db.RegisterArchiveGrant(8,owner.Device,Read(root,"conflict"))=="stale_or_conflicting_generation");
            Check(db.ArchiveAuthorized(8,owner.Device,owner.Device,1,2));
            Check(db.ArchiveAuthorized(8,owner.Device,reader.Device,1,1));
            Check(!db.ArchiveAuthorized(8,owner.Device,reader.Device,1,2));
            Check(!db.ArchiveAuthorized(8,owner.Device,other.Device,1,1));
            Check(!db.ArchiveAuthorized(9,owner.Device,reader.Device,1,1));
            Check(db.ArchiveDescriptors(8,reader.Device).Count==3&&db.ArchiveGrants(8,reader.Device).Count==2);
            Check(db.RegisterArchiveGrant(8,owner.Device,revoke)=="accepted");
            Check(!db.ArchiveAuthorized(8,owner.Device,reader.Device,1,1));
            Check(!db.ArchiveAuthorized(8,owner.Device,reader.Device,2,1));
            Check(db.RegisterArchiveGrant(8,owner.Device,read)=="stale_or_conflicting_generation");
            Check(db.RegisterArchiveGrant(8,owner.Device,reenroll)=="accepted");
            Check(db.ArchiveAuthorized(8,owner.Device,reader.Device,3,1));
        }
        using(var db=new AccountDatabase(path)){
            Check(db.ArchiveAuthorized(8,owner.Device,reader.Device,3,1));
            Check(db.RegisterArchiveGrant(8,owner.Device,revoke)=="stale_or_conflicting_generation");
            db.Execute("DELETE FROM devices WHERE public_identity='"+reader.Device+"';");
            Check(!db.ArchiveAuthorized(8,owner.Device,reader.Device,3,1));
            Check(db.AddDevice(8,reader.Device));
            Check(!db.ArchiveAuthorized(8,owner.Device,reader.Device,3,1));
            Check(db.RegisterArchiveGrant(8,owner.Device,reenroll)=="device_removed_reenrollment_required");
            Check(db.RegisterArchiveGrant(8,owner.Device,Read(root,"reenroll-after-removal"))=="accepted");
            Check(db.ArchiveAuthorized(8,owner.Device,reader.Device,4,1));
            db.Execute("DELETE FROM devices WHERE public_identity='"+owner.Device+"';");
            Check(!db.ArchiveAuthorized(8,owner.Device,reader.Device,4,1));
            Check(db.ArchiveGrants(8,reader.Device).All(row=>!(bool)row["currentlyEligible"]));
            Check(db.AddDevice(8,owner.Device));
            Check(!db.ArchiveAuthorized(8,owner.Device,reader.Device,4,1));
        }
        Console.WriteLine("PASS: native/server signed archive metadata interoperability; same-account denial, exact identity/account binding, signature/tamper, capability separation, monotonic revocation/re-enrollment, restart and device-removal tombstones.");
        return 0;
    }catch(Exception error){Console.Error.WriteLine(error.GetType().Name+": "+error.Message);return 1;}}
}
