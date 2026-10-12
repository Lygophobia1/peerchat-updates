using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using PeerChat.Server;

// Uses the real native Create event, not a synthetic proof fixture. This
// catches a client/server wire-format mismatch before physical group testing.
internal static class GroupCreationNativeContractTest {
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl)] static extern IntPtr ui_create();
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl)] static extern void ui_destroy(IntPtr handle);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)] static extern int ui_start(IntPtr handle,string directory,ushort port,string name);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)] static extern int ui_group_account(IntPtr handle,ulong account,string name);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)] static extern IntPtr ui_group_create(IntPtr handle,string name);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)] static extern IntPtr ui_group_create_proof(IntPtr handle,string group);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)] static extern int ui_authenticated_group_roster_evidence(IntPtr handle,ulong account,ulong generation,string group,string legacy,ulong rosterGeneration,string distributor,string members,[In] byte[] proof,uint size);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)] static extern int ui_authenticated_group_roster_proofs(IntPtr handle,ulong account,ulong generation,string group,string legacy,ulong rosterGeneration,string distributor,string members,[In] byte[] proof,uint size);
    static byte[] Evidence(byte[] root,ulong count=0){
        using(var stream=new MemoryStream())using(var writer=new BinaryWriter(stream)){
            writer.Write(new byte[]{(byte)'P',(byte)'T',1,5});var domain=System.Text.Encoding.UTF8.GetBytes("PeerChat/group-roster-evidence/v1");
            writer.Write((ulong)domain.Length);writer.Write(domain);writer.Write((ulong)root.Length);writer.Write(root);writer.Write(count);return stream.ToArray();
        }
    }
    static void Require(bool value,string message){if(!value)throw new Exception(message);}
    static void CreatePreDistributorRosterDatabase(string path){
        IntPtr database,error;
        Require(Sqlite.sqlite3_open16(path,out database)==0,"Old-schema test database could not open.");
        try{
            const string schema="CREATE TABLE group_rosters(group_id TEXT PRIMARY KEY, legacy_group_id TEXT NOT NULL, owner_account_id INTEGER NOT NULL, owner_identity TEXT NOT NULL, roster_generation INTEGER NOT NULL, creation_event_id TEXT NOT NULL DEFAULT '', created_utc INTEGER NOT NULL);";
            int result=Sqlite.sqlite3_exec(database,schema,IntPtr.Zero,IntPtr.Zero,out error);
            if(error!=IntPtr.Zero)Sqlite.sqlite3_free(error);
            Require(result==0,"Old-schema test database could not initialize.");
        }finally{Sqlite.sqlite3_close(database);}
    }
    public static int Main(){
        string profile=Path.Combine(Path.GetTempPath(),"PeerChat-group-proof-"+Guid.NewGuid().ToString("N"));IntPtr handle=IntPtr.Zero;
        try{
            handle=ui_create();Require(handle!=IntPtr.Zero,"Native core did not initialize.");
            Require(ui_start(handle,profile,0,"Group proof test")!=0,"Native profile did not start.");
            Require(ui_group_account(handle,1,"Owner")!=0,"Account was not bound to the native group owner.");
            string group=Marshal.PtrToStringUni(ui_group_create(handle,"New server roster group"));Require(group!=null&&group.Length==32,"Native group creation was not queued.");
            string encoded="";for(int i=0;i<20&&encoded.Length==0;i++){encoded=Marshal.PtrToStringUni(ui_group_create_proof(handle,group))??"";if(encoded.Length==0)Thread.Sleep(50);}
            Require(encoded.Length>0,"Native owner Create event yielded no signed proof.");
            GroupRosterProof proof=GroupRosterProof.Parse(encoded);
            Require(proof.LegacyGroupId==group&&proof.OwnerAccountId==1&&proof.OwnerIdentity.Length==64&&proof.ServerGroupId.Length==32,"Server did not accept the actual native owner Create proof.");
            byte[] root=Convert.FromBase64String(encoded);string members="1|"+proof.OwnerIdentity+"|owner|member";
            Require(ui_authenticated_group_roster_evidence(handle,1,1,proof.ServerGroupId,group,1,proof.OwnerIdentity,members,root,(uint)root.Length)!=0,"Native root projection rejected real signed creation");
            var bundle=Evidence(root);
            Require(ui_authenticated_group_roster_proofs(handle,1,1,proof.ServerGroupId,group,1,proof.OwnerIdentity,members,bundle,(uint)bundle.Length)!=0,"Copied native evidence boundary rejected valid root");
            var badBundle=Evidence(root,33);
            Require(ui_authenticated_group_roster_proofs(handle,1,1,proof.ServerGroupId,group,1,proof.OwnerIdentity,members,badBundle,(uint)badBundle.Length)==0,"Evidence count limit bypassed");
            badBundle=(byte[])bundle.Clone();badBundle[badBundle.Length-9]^=1;
            Require(ui_authenticated_group_roster_proofs(handle,1,1,proof.ServerGroupId,group,1,proof.OwnerIdentity,members,badBundle,(uint)badBundle.Length)==0,"Copied evidence admitted altered root");
            byte[] damaged=(byte[])root.Clone();damaged[damaged.Length-1]^=1;
            Require(ui_authenticated_group_roster_evidence(handle,1,1,proof.ServerGroupId,group,1,proof.OwnerIdentity,members,damaged,(uint)damaged.Length)==0,"Adapter admitted damaged root");
            Require(ui_authenticated_group_roster_evidence(handle,1,1,new string('f',32),group,1,proof.OwnerIdentity,members,root,(uint)root.Length)==0,"Adapter admitted wrong root namespace");
            Require(ui_authenticated_group_roster_evidence(handle,1,1,proof.ServerGroupId,group,1,proof.OwnerIdentity,members,root,2049)==0,"Adapter failed proof size bound");
            string databasePath=Path.Combine(profile,"roster-test.sqlite");
            CreatePreDistributorRosterDatabase(databasePath);
            using(var database=new AccountDatabase(databasePath)){
                Require(database.CreateAccountWithDevice("owner",new byte[16],new byte[32],proof.OwnerIdentity)==1,"Server test owner account was not created.");
                var roster=database.RegisterGroup(1,proof.OwnerIdentity,proof);
                Require(roster!=null&&Convert.ToInt64(roster["rosterGeneration"])==1,"Native group was not registered with an initialized server roster.");
                Require(Convert.ToString(roster["distributorIdentity"])==proof.OwnerIdentity,"Old-schema migration did not initialize the owner distributor.");
                Require(database.GroupRostersForDevice(1,proof.OwnerIdentity).Count==1,"Registered owner cannot retrieve the authoritative group roster.");
            }
            using(var database=new AccountDatabase(databasePath))Require(database.GroupRostersForDevice(1,proof.OwnerIdentity).Count==1,"Restart lost the migrated owner roster.");
            Console.WriteLine("PASS: real native group Create proof registers an owner-only authoritative server roster at generation 1, including migration of the earlier roster schema.");return 0;
        }catch(Exception error){Console.Error.WriteLine("FAIL: "+error.GetType().Name+": "+error.Message);return 1;}
        finally{if(handle!=IntPtr.Zero)ui_destroy(handle);try{if(Directory.Exists(profile))Directory.Delete(profile,true);}catch{}}
    }
}
