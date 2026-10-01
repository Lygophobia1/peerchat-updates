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
    static void Require(bool value,string message){if(!value)throw new Exception(message);}
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
            using(var database=new AccountDatabase(Path.Combine(profile,"roster-test.sqlite"))){
                Require(database.CreateAccountWithDevice("owner",new byte[16],new byte[32],proof.OwnerIdentity)==1,"Server test owner account was not created.");
                var roster=database.RegisterGroup(1,proof.OwnerIdentity,proof);
                Require(roster!=null&&Convert.ToInt64(roster["rosterGeneration"])==1,"Native group was not registered with an initialized server roster.");
                Require(database.GroupRostersForDevice(1,proof.OwnerIdentity).Count==1,"Registered owner cannot retrieve the authoritative group roster.");
            }
            Console.WriteLine("PASS: real native group Create proof registers an owner-only authoritative server roster at generation 1.");return 0;
        }catch(Exception error){Console.Error.WriteLine("FAIL: "+error.GetType().Name+": "+error.Message);return 1;}
        finally{if(handle!=IntPtr.Zero)ui_destroy(handle);try{if(Directory.Exists(profile))Directory.Delete(profile,true);}catch{}}
    }
}
