using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;

namespace PeerChatDesktop {
// This optional DPAPI cache contains labels only, never authorization state.
// It does not overwrite or migrate the older profile-local history nicknames.
public sealed class OwnDeviceNameCache {
    readonly string path;readonly JavaScriptSerializer json=new JavaScriptSerializer();Dictionary<string,string> labels=new Dictionary<string,string>(StringComparer.Ordinal);
    public OwnDeviceNameCache(string profile){path=Path.Combine(profile,"shared-device-names.dat");try{if(File.Exists(path)&&new FileInfo(path).Length<=262144){var bytes=ProtectedData.Unprotect(File.ReadAllBytes(path),Encoding.UTF8.GetBytes("PeerChat/SharedDeviceNames/v1"),DataProtectionScope.CurrentUser);try{var loaded=json.Deserialize<Dictionary<string,string>>(Encoding.UTF8.GetString(bytes));if(loaded!=null&&loaded.Count<=4096)foreach(var pair in loaded){var parts=pair.Key.Split(':');long account;string name=CosmeticDeviceNames.Clean(pair.Value);if(parts.Length==2&&Int64.TryParse(parts[0],out account)&&account>0&&CosmeticDeviceNames.Identity(parts[1])&&name!=null)labels[pair.Key]=name;}}finally{Array.Clear(bytes,0,bytes.Length);}}}catch{/* Corrupt optional labels never block profile loading. */}}
    static string Key(long account,string peer){return account.ToString(CultureInfo.InvariantCulture)+":"+peer;}
    public string Get(long account,string peer){string value;return account>0&&CosmeticDeviceNames.Identity(peer)&&labels.TryGetValue(Key(account,peer),out value)?value:null;}
    public void Update(AccountDeviceSnapshot snapshot){if(snapshot==null||snapshot.Account<=0)return;var next=new Dictionary<string,string>(labels,StringComparer.Ordinal);foreach(string key in next.Keys.Where(k=>k.StartsWith(snapshot.Account.ToString(CultureInfo.InvariantCulture)+":",StringComparison.Ordinal)).ToArray())next.Remove(key);foreach(var device in snapshot.Devices){string name=CosmeticDeviceNames.Clean(device.Name);if(name!=null)next[Key(snapshot.Account,device.Identity)]=name;}if(next.Count>4096)return;bool changed=next.Count!=labels.Count||next.Any(p=>!labels.ContainsKey(p.Key)||labels[p.Key]!=p.Value);labels=next;if(!changed)return;string temp=path+"."+Guid.NewGuid().ToString("N")+".tmp";byte[] plain=Encoding.UTF8.GetBytes(json.Serialize(next));try{Directory.CreateDirectory(Path.GetDirectoryName(path));File.WriteAllBytes(temp,ProtectedData.Protect(plain,Encoding.UTF8.GetBytes("PeerChat/SharedDeviceNames/v1"),DataProtectionScope.CurrentUser));if(File.Exists(path))File.Replace(temp,path,null);else File.Move(temp,path);}catch{/* Labels remain available in memory; durable messages are unaffected. */}finally{Array.Clear(plain,0,plain.Length);try{if(File.Exists(temp))File.Delete(temp);}catch{}}}
}
// Only authenticated own-account metadata belongs here. Contact pins and history
// grants remain separate; names are never an identity or an authorization input.
public sealed class ManagedAccountDevice {
    public string Identity,Name,State,Platform,Version,CacheCapability;
    public long CreatedUtc;
    public bool Current,Online;
    public string DisplayName {get{return String.IsNullOrWhiteSpace(Name)?(Current?"This device":"Unnamed device"):Name;}}
    public string Status {get{return State=="revoked"?"Revoked":Online?"Online · authenticated presence":"Offline · no current authenticated presence";}}
    public string CacheDisplay {get{return CacheCapability=="supported"?"Registered support":CacheCapability=="not_registered"?"Not registered":CacheCapability=="revoked"?"Revoked":CacheCapability;}}
}
public sealed class AccountDeviceSnapshot {
    public long Account,Revision;public string Epoch;public ManagedAccountDevice[] Devices;
    public static bool Hex(string value,int size){return value!=null&&value.Length==size&&value.All(c=>c>='0'&&c<='9'||c>='a'&&c<='f');}
    internal static string Text(Dictionary<string,object> row,string key){object v;return row.TryGetValue(key,out v)?v as string:null;}
    internal static long Number(Dictionary<string,object> row,string key){object v;long n;if(!row.TryGetValue(key,out v)||!Int64.TryParse(Convert.ToString(v,CultureInfo.InvariantCulture),NumberStyles.None,CultureInfo.InvariantCulture,out n)||n<0)throw new InvalidDataException("Invalid device-management number.");return n;}
    static bool Flag(Dictionary<string,object> row,string key){object v;if(!row.TryGetValue(key,out v)||!(v is bool))throw new InvalidDataException("Invalid device-management flag.");return (bool)v;}
    public static string NormalizeName(string name){
        if(name==null)throw new InvalidDataException("Enter a device name.");
        name=name.Trim().Normalize(NormalizationForm.FormC);
        if(name.Length<1||name.Length>64||Encoding.UTF8.GetByteCount(name)>256)throw new InvalidDataException("Use a name of 1–64 characters.");
        for(int i=0;i<name.Length;i++){var kind=CharUnicodeInfo.GetUnicodeCategory(name,i);if(kind==UnicodeCategory.Control||kind==UnicodeCategory.Format||kind==UnicodeCategory.Surrogate)throw new InvalidDataException("Device names cannot contain control or invisible formatting characters.");if(Char.IsHighSurrogate(name[i]))i++;}
        return name;
    }
    public static AccountDeviceSnapshot Parse(Dictionary<string,object> row,long account,string local){
        if(row==null||account<=0||!Hex(local,64)||Number(row,"deviceManagementVersion")!=1||Number(row,"accountId")!=account||!Hex(Text(row,"serverEpoch"),32))throw new InvalidDataException("Device inventory account or server binding was rejected.");
        object items;if(!row.TryGetValue("devices",out items)||!(items is IEnumerable)||items is string)throw new InvalidDataException("Device inventory is missing.");
        var devices=new List<ManagedAccountDevice>();var identities=new HashSet<string>(StringComparer.Ordinal);
        foreach(object item in (IEnumerable)items){var d=item as Dictionary<string,object>;if(d==null||devices.Count>=64)throw new InvalidDataException("Invalid device inventory.");string identity=Text(d,"publicIdentity"),state=Text(d,"state"),name=Text(d,"name")??"";
            if(!Hex(identity,64)||!identities.Add(identity)||(state!="active"&&state!="revoked")||name.Length>64||(name.Length>0&&NormalizeName(name)!=name))throw new InvalidDataException("Invalid device inventory identity or state.");
            bool current=Flag(d,"currentDevice"),online=Flag(d,"online");if(current!=(identity==local)||state=="revoked"&&online)throw new InvalidDataException("Device inventory current-device binding was rejected.");
            devices.Add(new ManagedAccountDevice{Identity=identity,Name=name,State=state,Current=current,Online=online,CreatedUtc=Number(d,"createdUtc"),Platform=SafeField(d,"platform"),Version=SafeField(d,"version"),CacheCapability=SafeField(d,"cacheCapability")});
        }
        if(!devices.Any(d=>d.Current&&d.State=="active"))throw new InvalidDataException("This device is not currently authorized in the inventory.");
        return new AccountDeviceSnapshot{Account=account,Epoch=Text(row,"serverEpoch"),Revision=Number(row,"authorityRevision"),Devices=devices.ToArray()};
    }
    static string SafeField(Dictionary<string,object> row,string key){string value=Text(row,key);if(String.IsNullOrEmpty(value)||value=="unavailable")return "Not available";if(value.Length>128||value.Any(Char.IsControl))throw new InvalidDataException("Invalid device metadata.");return value;}
    public bool CanRevoke(ManagedAccountDevice device){return device!=null&&!device.Current&&device.State=="active"&&Devices.Count(d=>d.State=="active")>1;}
    public static bool CanApply(AccountDeviceSnapshot previous,AccountDeviceSnapshot next){
        if(next==null)return false;if(previous==null)return true;if(previous.Account!=next.Account||next.Revision<previous.Revision)return false;
        foreach(var old in previous.Devices.Where(d=>d.State=="revoked"))if(!next.Devices.Any(d=>d.Identity==old.Identity&&d.State=="revoked"))return false;
        if(previous.Epoch==next.Epoch&&previous.Revision==next.Revision){if(previous.Devices.Length!=next.Devices.Length)return false;foreach(var old in previous.Devices)if(!next.Devices.Any(d=>d.Identity==old.Identity&&d.Name==old.Name&&d.State==old.State))return false;}
        return true;
    }
}
public sealed partial class DiscoveryClient {
    internal Dictionary<string,object> DeviceManagementRequest(string operation,string device,Dictionary<string,object> fields){
        if(!new[]{"device_inventory","device_metadata","device_rename","device_revoke_challenge","device_revoke"}.Contains(operation)||!MatchesAttachmentDeviceSession(AccountId,device))throw new InvalidOperationException("Device management requires a verified device session.");
        var request=new Dictionary<string,object>(fields??new Dictionary<string,object>());request["sessionToken"]=SessionToken;request["publicIdentity"]=device;return Request(operation,request);
    }
}
public sealed class DeviceManagementClient {
    readonly DiscoveryClient client;readonly string device;readonly long account;readonly Func<byte[],byte[]> signer;readonly Func<bool> current;
    public DeviceManagementClient(DiscoveryClient client,long account,string device,Func<byte[],byte[]> signer,Func<bool> current){this.client=client;this.account=account;this.device=device;this.signer=signer;this.current=current;Check();}
    void Check(){if(client==null||!client.MatchesAttachmentDeviceSession(account,device)||current!=null&&!current())throw new InvalidOperationException("Device management account or profile changed.");}
    Dictionary<string,object> Send(string op,Dictionary<string,object> fields){Check();var value=client.DeviceManagementRequest(op,device,fields);Check();return value;}
    public AccountDeviceSnapshot Inventory(){return AccountDeviceSnapshot.Parse(Send("device_inventory",null),account,device);}
    public AccountDeviceSnapshot PublishMetadata(string platform,string version){return AccountDeviceSnapshot.Parse(Send("device_metadata",new Dictionary<string,object>{{"platform",platform},{"appVersion",version}}),account,device);}
    public AccountDeviceSnapshot Rename(AccountDeviceSnapshot snapshot,string target,string name){ValidateTarget(snapshot,target,false);return AccountDeviceSnapshot.Parse(Send("device_rename",new Dictionary<string,object>{{"targetPublicIdentity",target},{"expectedRevision",snapshot.Revision.ToString(CultureInfo.InvariantCulture)},{"name",AccountDeviceSnapshot.NormalizeName(name)}}),account,device);}
    void ValidateTarget(AccountDeviceSnapshot snapshot,string target,bool revoke){Check();if(snapshot==null||snapshot.Account!=account||!AccountDeviceSnapshot.Hex(target,64))throw new InvalidDataException("Refresh device inventory before making changes.");var entry=snapshot.Devices.SingleOrDefault(d=>d.Identity==target);if(entry==null||entry.State!="active"||revoke&&!snapshot.CanRevoke(entry))throw new InvalidDataException("The selected device cannot be changed. Self and last-device revocation are not supported.");}
    public AccountDeviceSnapshot Revoke(AccountDeviceSnapshot snapshot,string target,string password){
        ValidateTarget(snapshot,target,true);if(String.IsNullOrEmpty(password))throw new InvalidDataException("Enter your account password to confirm revocation.");
        long issued=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();var fields=new Dictionary<string,object>{{"targetPublicIdentity",target},{"expectedRevision",snapshot.Revision.ToString(CultureInfo.InvariantCulture)},{"password",password}};
        Dictionary<string,object> challenge;try{challenge=Send("device_revoke_challenge",fields);}finally{fields.Remove("password");password=null;}
        Check();var proof=CreateProof(signer,challenge,account,device,target,snapshot.Revision,client.CertificateSha256,Hash(Encoding.UTF8.GetBytes(client.SessionToken)),issued);Check();proof["targetPublicIdentity"]=target;proof["expectedRevision"]=snapshot.Revision.ToString(CultureInfo.InvariantCulture);
        return AccountDeviceSnapshot.Parse(Send("device_revoke",proof),account,device);
    }
    static string Hash(byte[] bytes){using(var hash=SHA256.Create())return BitConverter.ToString(hash.ComputeHash(bytes)).Replace("-","").ToLowerInvariant();}
    internal static Dictionary<string,object> CreateProof(Func<byte[],byte[]> signer,Dictionary<string,object> challenge,long account,string actor,string target,long revision,string pin,string binding,long localIssued){
        string epoch=AccountDeviceSnapshot.Text(challenge,"serverEpoch"),id=AccountDeviceSnapshot.Text(challenge,"challengeId");long issued=AccountDeviceSnapshot.Number(challenge,"issuedUtc"),expires=AccountDeviceSnapshot.Number(challenge,"expiresUtc"),now=DateTimeOffset.UtcNow.ToUnixTimeSeconds();pin=(pin??"").ToLowerInvariant();
        if(signer==null||AccountDeviceSnapshot.Text(challenge,"purpose")!="revoke"||AccountDeviceSnapshot.Number(challenge,"accountId")!=account||AccountDeviceSnapshot.Text(challenge,"publicIdentity")!=actor||AccountDeviceSnapshot.Text(challenge,"targetPublicIdentity")!=target||AccountDeviceSnapshot.Number(challenge,"expectedRevision")!=revision||AccountDeviceSnapshot.Text(challenge,"serverFingerprint")!=pin||AccountDeviceSnapshot.Text(challenge,"sessionBinding")!=binding||!AccountDeviceSnapshot.Hex(pin,64)||!AccountDeviceSnapshot.Hex(binding,64)||!AccountDeviceSnapshot.Hex(epoch,32)||!AccountDeviceSnapshot.Hex(id,64)||!AccountDeviceSnapshot.Hex(actor,64)||!AccountDeviceSnapshot.Hex(target,64)||actor==target||issued>now||now>=expires||expires-issued<1||expires-issued>60||DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()-localIssued<0||DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()-localIssued>60000)throw new InvalidDataException("Device revocation challenge failed scope or freshness verification.");
        byte[] canonical=Encoding.ASCII.GetBytes("PeerChat device management v1|revoke|"+account.ToString(CultureInfo.InvariantCulture)+"|"+actor+"|"+target+"|"+revision.ToString(CultureInfo.InvariantCulture)+"|"+pin+"|"+epoch+"|"+issued.ToString(CultureInfo.InvariantCulture)+"|"+expires.ToString(CultureInfo.InvariantCulture)+"|"+id+"|"+binding);
        byte[] supplied;try{supplied=Convert.FromBase64String(AccountDeviceSnapshot.Text(challenge,"signatureInput")??"");}catch(FormatException){throw new InvalidDataException("Invalid device revocation challenge.");}if(!supplied.SequenceEqual(canonical))throw new InvalidDataException("Device revocation challenge signature input was rejected.");
        byte[] input;using(var memory=new MemoryStream())using(var writer=new BinaryWriter(memory,Encoding.UTF8)){writer.Write(new byte[]{80,84,1,30});Write(writer,Encoding.UTF8.GetBytes("PeerChat/DeviceManagementProof/v1"));writer.Write((ulong)localIssued);writer.Write((ulong)account);Write(writer,Encoding.UTF8.GetBytes(actor));Write(writer,Encoding.UTF8.GetBytes(target));writer.Write((ulong)revision);Write(writer,Encoding.UTF8.GetBytes(pin));Write(writer,Encoding.UTF8.GetBytes(epoch));writer.Write((ulong)issued);writer.Write((ulong)expires);Write(writer,Encoding.UTF8.GetBytes(id));Write(writer,Encoding.UTF8.GetBytes(binding));Write(writer,canonical);input=memory.ToArray();}
        byte[] key,signature;using(var result=new MailboxExecution.Record(signer(input),4)){key=result.Bytes();signature=result.Bytes();result.End();}if(key.Length!=72||signature.Length!=64||Hash(key)!=actor)throw new InvalidDataException("Device revocation proof was rejected.");
        return new Dictionary<string,object>{{"challengeId",id},{"publicKey",Convert.ToBase64String(key)},{"signature",Convert.ToBase64String(signature)}};
    }
    static void Write(BinaryWriter writer,byte[] data){writer.Write((ulong)data.Length);writer.Write(data);}
    public static string Failure(Exception error){var rejected=error as DiscoveryRequestException;if(rejected!=null){switch(rejected.ErrorCode){case "unknown_type":case "unsupported_operation":case "unsupported_request":return "This server does not support Device Manager. Existing messaging is unchanged.";case "revision_conflict":case "device_management_revision_conflict":return "Device information changed on another client. Refresh before retrying.";case "step_up_failed":case "device_management_step_up_failed":return "The account password could not be verified. Nothing was revoked.";case "device_management_rate_limited":return "Too many recent verification attempts. Wait a few seconds, then try again; nothing was revoked by this attempt.";case "self_revoke_denied":case "device_management_self_revoke_denied":case "device_management_last_device_denied":return "This device and the last active device cannot be revoked here.";case "device_revoked":case "remember_revoked":return "This device is revoked. Its keys and local history were preserved; re-enrollment is not automatic.";case "device_management_session_required":return "Verify this device before managing your account's devices.";default:return "Device management was rejected ("+rejected.ErrorCode+"). Refresh and retry; no authorization change is assumed.";}}
        return error is InvalidDataException?error.Message:"Device management could not finish. Check the connection and refresh; existing keys, history and queues were preserved.";
    }
}

public partial class DesktopApplication {
    OwnDeviceNameCache sharedOwnNames;OwnDeviceNameCache SharedOwnNames {get{return sharedOwnNames??(sharedOwnNames=new OwnDeviceNameCache(directory));}}
    string SharedDeviceName(long account,string peer){if(account<=0||!CosmeticDeviceNames.Identity(peer))return null;if(managedDevices!=null&&managedDevices.Account==account){var device=managedDevices.Devices.FirstOrDefault(d=>d.Identity==peer);if(device!=null)return CosmeticDeviceNames.Clean(device.Name);}string own=SharedOwnNames.Get(account,peer);if(own!=null)return own;var friend=conversationIndex==null?null:conversationIndex.Records.FirstOrDefault(d=>d.AccountId==account);string label;return friend!=null&&friend.DeviceNames!=null&&friend.DeviceNames.TryGetValue(peer,out label)?CosmeticDeviceNames.Clean(label):null;}
    long sharedDeviceNameRevision;
    void RefreshSharedDeviceNames(){sharedDeviceNameRevision++;if(model!=null)model.RefreshDeviceNames();var changed=HistoryPresentationChanged;if(changed!=null)changed(this,EventArgs.Empty);}
    AccountDeviceSnapshot managedDevices;DiscoveryClient managedDeviceOwner,managedDeviceSession;ulong managedDeviceGeneration;string managedDeviceToken;bool managedDeviceBusy;DateTime managedDeviceNextUtc=DateTime.MinValue;string managedDeviceStatus="Connect and verify this device to view your account's devices.";event EventHandler ManagedDevicesChanged;
    void PublishManagedDevices(){var handler=ManagedDevicesChanged;if(handler!=null)handler(this,EventArgs.Empty);}
    bool ManagedContext(DiscoveryClient owner,ulong generation,string token,string actor){return !closing&&discovery==owner&&discoverySessionGeneration==generation&&owner.SessionToken==token&&model.OwnId==actor;}
    async void PumpDeviceManagement(){
        if(closing||managedDeviceBusy||discovery==null||discovery.AccountId<=0||mailboxContextAccount!=discovery.AccountId)return;
        if(managedDeviceOwner!=discovery||managedDeviceGeneration!=discoverySessionGeneration||managedDeviceToken!=discovery.SessionToken){managedDevices=null;managedDeviceStatus="Refreshing verified device information…";managedDeviceNextUtc=DateTime.MinValue;managedDeviceOwner=discovery;managedDeviceGeneration=discoverySessionGeneration;managedDeviceToken=discovery.SessionToken;var old=managedDeviceSession;managedDeviceSession=null;if(old!=null)_=Task.Run(()=>old.CloseRpc());PublishManagedDevices();}
        if(DateTime.UtcNow<managedDeviceNextUtc)return;
        managedDeviceNextUtc=DateTime.UtcNow.AddSeconds(60);
        await RunDeviceManagement(null,false);
    }
    async Task<bool> RunDeviceManagement(Func<DeviceManagementClient,AccountDeviceSnapshot> operation,bool enroll){
        var owner=discovery;var generation=discoverySessionGeneration;var token=owner==null?"":owner.SessionToken;var actor=model.OwnId;long account=owner==null?0:owner.AccountId;
        if(managedDeviceBusy||owner==null||account<=0||mailboxContextAccount!=account)return false;
        managedDeviceBusy=true;PublishManagedDevices();DiscoveryClient opened=null;
        try{var existing=managedDeviceSession;
            var result=await Task.Run(()=>{
                Func<bool> same=()=>ManagedContext(owner,generation,token,actor);
                if(!same())throw new InvalidOperationException();
                var session=existing;if(session==null){if(!enroll&&!AttachmentDeviceSession.HasCredential(owner,account,actor,directory))throw new InvalidDataException("Select Verify this device to use its existing identity key for account-scoped device management.");session=opened=AttachmentDeviceSession.Open(owner,core,account,actor,directory,enroll,same);}
                var client=new DeviceManagementClient(session,account,actor,input=>core.Mailbox(13,input),same);
                return operation==null?(opened!=null?client.PublishMetadata("Windows",PeerChat.VersionInfo.Display):client.Inventory()):operation(client);
            });
            if(!ManagedContext(owner,generation,token,actor))return false;
            if(!AccountDeviceSnapshot.CanApply(managedDevices,result))throw new InvalidDataException("A stale device snapshot was rejected. Refresh after reconnecting.");
            if(opened!=null){managedDeviceSession=opened;opened=null;}managedDeviceOwner=owner;managedDeviceGeneration=generation;managedDeviceToken=token;managedDevices=result;SharedOwnNames.Update(result);RefreshSharedDeviceNames();managedDeviceStatus="Verified account "+account+" · revision "+result.Revision+" · updated "+DateTime.Now.ToString("t");managedDeviceNextUtc=DateTime.UtcNow.AddSeconds(60);identityNextUtc=DateTime.MinValue;return true;
        }catch(Exception error){if(ManagedContext(owner,generation,token,actor)){managedDeviceStatus=DeviceManagementClient.Failure(error);var stale=managedDeviceSession;managedDeviceSession=null;if(stale!=null)_=Task.Run(()=>stale.CloseRpc());managedDeviceNextUtc=DateTime.UtcNow.AddSeconds(60);}return false;}
        finally{if(opened!=null){var unused=opened;_=Task.Run(()=>unused.CloseRpc());}managedDeviceBusy=false;PublishManagedDevices();}
    }
    readonly Dictionary<long,DeviceIdentitySnapshot> identitySnapshots=new Dictionary<long,DeviceIdentitySnapshot>();
    bool identityBusy,identityRegistered;DateTime identityNextUtc=DateTime.MinValue;int identityOffset,identityPriorityOffset;long identityUrgentAccount;string identityStatus="Verify this device to refresh enrollment evidence.";
    DiscoveryClient identityOwner;ulong identitySession,identityProfile,identityGeneration;string identityPin;
    event EventHandler IdentityDirectoryChanged;
    void PublishIdentityDirectory(){var changed=IdentityDirectoryChanged;if(changed!=null)changed(this,EventArgs.Empty);}
    bool IdentityContext(DiscoveryClient owner,ulong session,ulong profile,ulong friends,string token,string pin,string actor,long account){
        return ManagedContext(owner,session,token,actor)&&profileSessionGeneration==profile&&authenticatedFriendSnapshotGeneration==friends&&
            mailboxContextAccount==account&&owner.CertificateSha256==pin;
    }
    DeviceIdentityDirectoryClient OpenIdentityClient(DiscoveryClient owner,ulong session,ulong profile,ulong friends,string token,string pin,string actor,long account,out DiscoveryClient opened){
        Func<bool> same=()=>IdentityContext(owner,session,profile,friends,token,pin,actor,account);
        if(!same()||!AttachmentDeviceSession.HasCredential(owner,account,actor,directory))throw new InvalidDataException("Verify this device before using enrollment evidence.");
        opened=AttachmentDeviceSession.Open(owner,core,account,actor,directory,false,same);
        return new DeviceIdentityDirectoryClient(opened,account,actor,checked((long)friends),input=>core.Mailbox(14,input),same);
    }
    long[] ActiveIdentityRecipients(bool storageAllowed){
        var accounts=new HashSet<long>();
        try{using(var record=new MailboxExecution.Record(core.Mailbox(2,null,"",storageAllowed),4)){
            long count=record.Number();if(count>64)throw new InvalidDataException("Mailbox pending count exceeds bound.");
            for(long index=0;index<count;index++){record.Text();record.Text();long account=record.Number();if(account>0)accounts.Add(account);}record.End();
        }}catch(Exception error){if(error is OutOfMemoryException)throw;}
        try{var snapshot=core.DmAttachments();foreach(var row in snapshot.items??new CoreAdapter.FileTransferData[0])
            if(row!=null&&row.dmCache&&row.sending&&row.account>0&&row.dmState!="complete"&&row.dmState!="failed"&&row.dmState!="canceled")accounts.Add(row.account);
        }catch(Exception error){if(error is OutOfMemoryException)throw;}
        return accounts.OrderBy(x=>x).ToArray();
    }
    async void PumpDeviceIdentity(){
        var owner=discovery;if(closing||identityBusy||owner==null||owner.AccountId<=0||mailboxContextAccount!=owner.AccountId||authenticatedFriendSnapshotGeneration==0||DateTime.UtcNow<identityNextUtc)return;
        if(identityOwner!=owner||identitySession!=discoverySessionGeneration||identityProfile!=profileSessionGeneration||identityGeneration!=authenticatedFriendSnapshotGeneration||identityPin!=owner.CertificateSha256){
            identitySnapshots.Clear();identityRegistered=false;identityOwner=owner;identitySession=discoverySessionGeneration;identityProfile=profileSessionGeneration;identityGeneration=authenticatedFriendSnapshotGeneration;identityPin=owner.CertificateSha256;identityOffset=identityPriorityOffset=0;PublishIdentityDirectory();
        }
        bool enrolled;try{enrolled=AttachmentDeviceSession.HasCredential(owner,owner.AccountId,model.OwnId,directory);}catch(Exception error){if(error is OutOfMemoryException)throw;enrolled=false;}
        if(!enrolled){identityStatus="Verify this device to publish its possession proof.";identityNextUtc=DateTime.UtcNow.AddSeconds(60);PublishIdentityDirectory();return;}
        var friends=conversationIndex==null?new long[0]:conversationIndex.Records.Where(x=>x.ActiveFriend&&x.AccountId>0).Select(x=>x.AccountId).Distinct().OrderBy(x=>x).ToArray();
        if(identityUrgentAccount>0&&!friends.Contains(identityUrgentAccount))identityUrgentAccount=0;
        // Idle contacts rotate in a bounded page. Active cached and mailbox work
        // gets its own page so a large contact list cannot starve pending jobs.
        var general=friends.Length<=8?friends:Enumerable.Range(0,8).Select(n=>friends[(identityOffset+n)%friends.Length]).ToArray();if(friends.Length>0)identityOffset=(identityOffset+general.Length)%friends.Length;
        long urgent=identityUrgentAccount;bool storageAllowed=mailboxStorageAllowed,registered=identityRegistered;
        identityBusy=true;identityNextUtc=DateTime.UtcNow.AddSeconds(20);PublishIdentityDirectory();
        var session=discoverySessionGeneration;var profile=profileSessionGeneration;var generation=authenticatedFriendSnapshotGeneration;var token=owner.SessionToken;var pin=owner.CertificateSha256;var actor=model.OwnId;long account=owner.AccountId;
        DiscoveryClient opened=null;try{
            var result=await Task.Run(()=>{
                var snapshots=new Dictionary<long,DeviceIdentitySnapshot>();var errors=new List<string>();
                var client=OpenIdentityClient(owner,session,profile,generation,token,pin,actor,account,out opened);
                bool submitted=false;if(!registered){client.RegisterSelf();submitted=true;}
                var own=client.Snapshot(account);
                if(!own.HasPossession(actor)){
                    if(!submitted){client.RegisterSelf();own=client.Snapshot(account);}
                    if(!own.HasPossession(actor))throw new InvalidDataException("This device's signed possession was not present in the authenticated directory.");
                }
                snapshots[account]=own;
                if(own.AuthorityProof.Length!=0){try{client.Apply(own);}catch(InvalidDataException){errors.Add("Own authority needs explicit selection or current evidence.");}catch(InvalidOperationException){errors.Add("Own authority needs explicit selection or current evidence.");}}
                else errors.Add("Choose this account's enrollment authority to enable device approval.");
                var active=ActiveIdentityRecipients(storageAllowed).Where(friends.Contains).ToArray();
                var priority=active.Length<=16?active:Enumerable.Range(0,16).Select(n=>active[(identityPriorityOffset+n)%active.Length]).ToArray();if(active.Length>0)identityPriorityOffset=(identityPriorityOffset+priority.Length)%active.Length;
                var targets=(urgent>0&&friends.Contains(urgent)?new[]{urgent}:new long[0]).Concat(priority).Concat(general).Distinct().Take(25).ToArray();
                foreach(long friend in targets){
                    if(!IdentityContext(owner,session,profile,generation,token,pin,actor,account))break;
                    try{var snapshot=client.Snapshot(friend);snapshots[friend]=snapshot;if(snapshot.AuthorityProof.Length!=0)client.Apply(snapshot);else errors.Add("Account "+friend+": no enrollment authority; verify directly when available.");}
                    catch(Exception error){if(error is OutOfMemoryException)throw;errors.Add("Account "+friend+": enrollment evidence needs verification or refresh.");}
                }
                return Tuple.Create(snapshots,errors,active.Length,true);
            });
            if(!IdentityContext(owner,session,profile,generation,token,pin,actor,account))return;
            foreach(var pair in result.Item1)identitySnapshots[pair.Key]=pair.Value;
            identityRegistered=result.Item4;
            if(identityUrgentAccount>0&&result.Item1.ContainsKey(identityUrgentAccount))identityUrgentAccount=0;
            identityNextUtc=DateTime.UtcNow.AddSeconds(result.Item3>0||identityUrgentAccount>0?10:20);
            identityStatus=result.Item2.Count==0?"Enrollment evidence refreshed at "+DateTime.Now.ToString("t")+".":String.Join(" ",result.Item2.Take(3));
        }catch(Exception error){if(IdentityContext(owner,session,profile,generation,token,pin,actor,account)){identityStatus=error is DiscoveryRequestException?"This server has no available enrollment directory: "+((DiscoveryRequestException)error).ErrorCode:"Enrollment evidence refresh failed; existing verified identities remain available.";identityNextUtc=DateTime.UtcNow.AddSeconds(60);}}
        finally{if(opened!=null){var stale=opened;_=Task.Run(()=>stale.CloseRpc());}identityBusy=false;PublishIdentityDirectory();}
    }
    async Task<bool> RefreshIdentityForCachedSend(long recipient,Func<bool> selected){
        var owner=discovery;if(owner==null||recipient<=0||selected==null||!selected())return false;
        var session=discoverySessionGeneration;var profile=profileSessionGeneration;var generation=authenticatedFriendSnapshotGeneration;
        var token=owner.SessionToken;var pin=owner.CertificateSha256;var actor=model.OwnId;long account=owner.AccountId;
        if(!IdentityContext(owner,session,profile,generation,token,pin,actor,account))return false;
        var until=DateTime.UtcNow.AddSeconds(8);
        while(identityBusy&&DateTime.UtcNow<until){await Task.Delay(100);if(!selected()||!IdentityContext(owner,session,profile,generation,token,pin,actor,account))return false;}
        if(identityBusy){Notify("Enrollment evidence is still refreshing. Nothing was queued; retry shortly.");return false;}
        identityBusy=true;PublishIdentityDirectory();
        DiscoveryClient opened=null;
        try{
            var result=await Task.Run(()=>{
                var client=OpenIdentityClient(owner,session,profile,generation,token,pin,actor,account,out opened);
                var current=client.Snapshot(recipient);
                bool applied=false;
                if(current.AuthorityProof.Length!=0){ // Inactive roots must reach native revocation reconciliation immediately.
                    try{applied=client.Apply(current);}catch(InvalidDataException){}catch(InvalidOperationException){}
                }
                return Tuple.Create(current,applied);
            });
            if(!selected()||!IdentityContext(owner,session,profile,generation,token,pin,actor,account))return false;
            identitySnapshots[recipient]=result.Item1;
            if(!result.Item2)identityStatus="Account "+recipient+": enrollment authority is unverified or unavailable; existing verified pins remain in force. Verify its full fingerprint to enroll other devices.";
            PublishIdentityDirectory();return true; // Native cache admission still decides whether the existing verified audience is sufficient.
        }catch(DiscoveryRequestException error){
            if(!selected()||!IdentityContext(owner,session,profile,generation,token,pin,actor,account))return false;
            if(error.ErrorCode=="unsupported_request")return true; // Older server: the existing verified-pin path remains authoritative.
            Notify("Recipient enrollment evidence could not be refreshed ("+error.ErrorCode+"). Nothing was queued.");return false;
        }catch(Exception error){
            if(error is OutOfMemoryException)throw;
            if(selected()&&IdentityContext(owner,session,profile,generation,token,pin,actor,account))Notify(error is InvalidDataException?error.Message:"Recipient enrollment evidence could not be verified. Nothing was queued.");
            return false;
        }finally{if(opened!=null){var stale=opened;_=Task.Run(()=>stale.CloseRpc());}identityBusy=false;PublishIdentityDirectory();}
    }
    async Task<bool> RunIdentityAction(Func<DeviceIdentityDirectoryClient,DeviceIdentitySnapshot> action){
        var owner=discovery;if(identityBusy||owner==null||owner.AccountId<=0||mailboxContextAccount!=owner.AccountId)return false;
        var session=discoverySessionGeneration;var profile=profileSessionGeneration;var generation=authenticatedFriendSnapshotGeneration;var token=owner.SessionToken;var pin=owner.CertificateSha256;var actor=model.OwnId;long account=owner.AccountId;
        identityBusy=true;PublishIdentityDirectory();DiscoveryClient opened=null;
        try{var result=await Task.Run(()=>action(OpenIdentityClient(owner,session,profile,generation,token,pin,actor,account,out opened)));
            if(!IdentityContext(owner,session,profile,generation,token,pin,actor,account))return false;
            identitySnapshots[result.Account]=result;identityStatus="Enrollment evidence updated at "+DateTime.Now.ToString("t")+".";identityNextUtc=DateTime.MinValue;return true;
        }catch(Exception error){if(IdentityContext(owner,session,profile,generation,token,pin,actor,account))identityStatus=error is InvalidDataException?error.Message:error is DiscoveryRequestException?"Enrollment request rejected: "+((DiscoveryRequestException)error).ErrorCode:"Enrollment request could not finish. Refresh and retry.";return false;}
        finally{if(opened!=null){var stale=opened;_=Task.Run(()=>stale.CloseRpc());}identityBusy=false;PublishIdentityDirectory();}
    }
    void ConfirmExactIdentity(Window owner,string title,string explanation,string fingerprint,Func<string,Task<bool>> action){
        if(!DeviceIdentityDirectoryClient.Hex(fingerprint))return;
        var body=new StackPanel{Margin=new Thickness(20)};var prompt=new Window{Title=title,Owner=owner,Width=560,SizeToContent=SizeToContent.Height,ResizeMode=ResizeMode.NoResize,Background=owner.Background,Foreground=owner.Foreground,Resources=owner.Resources,WindowStartupLocation=WindowStartupLocation.CenterOwner,Content=body};
        body.Children.Add(HistoryTitle(title));body.Children.Add(HistoryText(explanation+"\n\nFull fingerprint:\n"+fingerprint+"\n\nCompare it through a trusted channel, then type all 64 characters to confirm."));
        var typed=new TextBox{MaxLength=64,Margin=new Thickness(0,12,0,12)};AutomationProperties.SetName(typed,"Confirmed full identity fingerprint");body.Children.Add(typed);var status=HistoryText("");body.Children.Add(status);
        var actions=new WrapPanel();body.Children.Add(actions);actions.Children.Add(HistoryButton("Cancel",()=>prompt.Close()));actions.Children.Add(HistoryButton("Confirm exact fingerprint",async()=>{
            if(typed.Text!=fingerprint){status.Text="The complete fingerprint does not match.";return;}
            prompt.IsEnabled=false;try{if(await action(typed.Text))prompt.Close();else status.Text=identityStatus;}finally{prompt.IsEnabled=true;}
        }));prompt.ShowDialog();
    }
    StackPanel DeviceManagerPanel(Window dialog){
        var panel=new StackPanel{Margin=new Thickness(4),MaxWidth=760,HorizontalAlignment=HorizontalAlignment.Stretch};panel.Children.Add(HistoryTitle("Device Manager"));panel.Children.Add(HistoryText("Manage verified devices belonging to this account only. Device names are labels, not fingerprints or trust decisions."));
        var status=HistoryText(managedDeviceStatus);panel.Children.Add(status);var buttons=new WrapPanel{Margin=new Thickness(0,12,0,12)};panel.Children.Add(buttons);
        var refresh=HistoryButton("Refresh",async()=>await RunDeviceManagement(null,false));var verify=HistoryButton("Verify this device",async()=>{if(Confirm("Verify this device","Use this profile's existing identity key to establish a device-bound management session? No keys, history, trust or grants will be replaced.","Verify"))await RunDeviceManagement(null,true);});buttons.Children.Add(refresh);buttons.Children.Add(verify);
        var identityLine=HistoryText(identityStatus);panel.Children.Add(identityLine);
        var identityActions=new WrapPanel{Margin=new Thickness(0,0,0,12)};panel.Children.Add(identityActions);
        var identityRefresh=HistoryButton("Refresh enrollment evidence",()=>{identityNextUtc=DateTime.MinValue;PumpDeviceIdentity();});identityActions.Children.Add(identityRefresh);
        var chooseAuthority=HistoryButton("Choose this device as enrollment authority",()=>{
            string local=model.OwnId;ConfirmExactIdentity(dialog,"Choose enrollment authority","This lets this device approve additional devices for this exact account. It does not grant history, group or cache access. Only choose it after checking its fingerprint.",local,typed=>RunIdentityAction(client=>client.ChooseAuthority(typed)));
        });identityActions.Children.Add(chooseAuthority);
        var verifyOwnAuthority=HistoryButton("Verify existing account authority",()=>{
            if(discovery==null||identityOwner!=discovery||identitySession!=discoverySessionGeneration||identityGeneration!=authenticatedFriendSnapshotGeneration)return;
            DeviceIdentitySnapshot evidence;if(!identitySnapshots.TryGetValue(discovery.AccountId,out evidence)||evidence.AuthorityProof.Length==0||!evidence.AuthorityActive)return;
            string fingerprint;try{fingerprint=evidence.AuthorityFingerprint();}catch(InvalidDataException){identityStatus="Account authority evidence is malformed.";PublishIdentityDirectory();return;}
            long ownAccount=discovery.AccountId;
            ConfirmExactIdentity(dialog,"Verify existing account authority","This account already has an enrollment authority. Compare this full fingerprint through a trusted channel. This one-time approval permits that authority's direct device endorsements; an existing device pin alone does not grant that scope.",fingerprint,typed=>RunIdentityAction(client=>{var fresh=client.Snapshot(ownAccount);if(fresh.AuthorityFingerprint()!=typed)throw new InvalidDataException("The account authority changed. Refresh and compare again.");client.Apply(fresh,typed);return fresh;}));
        });identityActions.Children.Add(verifyOwnAuthority);
        var contactAuthority=HistoryButton("Verify contact enrollment authority",()=>{
            var selected=model.Groups?null:model.Selected;DeviceIdentitySnapshot evidence;
            if(discovery==null||identityOwner!=discovery||identitySession!=discoverySessionGeneration||identityGeneration!=authenticatedFriendSnapshotGeneration||identityPin!=discovery.CertificateSha256||selected==null||selected.AccountId<=0||selected.Contact==null||!selected.IsFriend||!identitySnapshots.TryGetValue(selected.AccountId,out evidence)||evidence.AuthorityProof.Length==0){identityStatus="Select a friend and refresh enrollment evidence first.";PublishIdentityDirectory();return;}
            string fingerprint;try{fingerprint=evidence.AuthorityFingerprint();}catch(InvalidDataException){identityStatus="Contact authority evidence is malformed.";PublishIdentityDirectory();return;}
            long targetAccount=selected.AccountId;string contactName=selected.Name;
            ConfirmExactIdentity(dialog,"Verify contact enrollment authority","One-time authorization for "+contactName+" (account "+targetAccount+"). Compare this authority fingerprint directly with the contact. Existing verified device pins do not grant enrollment authority.",fingerprint,typed=>RunIdentityAction(client=>{var fresh=client.Snapshot(targetAccount);if(fresh.AuthorityFingerprint()!=typed)throw new InvalidDataException("The contact authority changed. Refresh and compare again.");client.Apply(fresh,typed);return fresh;}));
        });identityActions.Children.Add(contactAuthority);
        var rows=new StackPanel();panel.Children.Add(rows);panel.Children.Add(HistoryText("Revocation preserves local history and queued ciphertext. It blocks future authorized server access; it cannot erase data or keys already held by a device. Self/last-device revocation and reactivation are unavailable."));
        Action render=()=>{
            status.Text=managedDeviceStatus;refresh.IsEnabled=verify.IsEnabled=!managedDeviceBusy&&discovery!=null;
            identityLine.Text=identityStatus;identityRefresh.IsEnabled=!identityBusy&&discovery!=null;
            DeviceIdentitySnapshot ownIdentity=null;bool hasOwnIdentity=discovery!=null&&identitySnapshots.TryGetValue(discovery.AccountId,out ownIdentity)&&ownIdentity.AuthorityProof.Length!=0;
            chooseAuthority.IsEnabled=!identityBusy&&discovery!=null&&managedDevices!=null&&managedDevices.Account==discovery.AccountId&&managedDeviceOwner==discovery&&managedDeviceGeneration==discoverySessionGeneration&&!hasOwnIdentity;
            verifyOwnAuthority.IsEnabled=!identityBusy&&hasOwnIdentity&&ownIdentity.AuthorityActive&&identityOwner==discovery&&identitySession==discoverySessionGeneration&&identityGeneration==authenticatedFriendSnapshotGeneration;
            var selectedContact=model.Groups?null:model.Selected;DeviceIdentitySnapshot selectedEvidence;
            contactAuthority.IsEnabled=!identityBusy&&discovery!=null&&identityOwner==discovery&&identitySession==discoverySessionGeneration&&identityGeneration==authenticatedFriendSnapshotGeneration&&identityPin==discovery.CertificateSha256&&selectedContact!=null&&selectedContact.IsFriend&&identitySnapshots.TryGetValue(selectedContact.AccountId,out selectedEvidence)&&selectedEvidence.AuthorityProof.Length!=0;
            rows.Children.Clear();var snapshot=managedDevices;if(snapshot==null||discovery==null||snapshot.Account!=discovery.AccountId||managedDeviceOwner!=discovery||managedDeviceGeneration!=discoverySessionGeneration){rows.Children.Add(HistoryText("No current verified inventory. Other accounts and historical contact identities are not listed here."));return;}
            foreach(var entry in snapshot.Devices.OrderByDescending(d=>d.Current).ThenBy(d=>d.State).ThenBy(d=>d.DisplayName,StringComparer.CurrentCultureIgnoreCase)){
                var content=new StackPanel{Margin=new Thickness(14)};var title=HistoryTitle(entry.DisplayName+(entry.Current?" · This device":""));title.FontSize=17;title.TextWrapping=TextWrapping.Wrap;content.Children.Add(title);content.Children.Add(HistoryText(entry.Identity.Substring(0,12)+"…"+entry.Identity.Substring(56)+" · "+entry.Status));content.Children.Add(HistoryText("Platform: "+entry.Platform+" · Version: "+entry.Version+"\nOffline attachments: "+entry.CacheDisplay+"\nEncrypted message capability: Not available"));
                var details=new StackPanel();var fingerprint=new TextBox{Text=entry.Identity,IsReadOnly=true,TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,6,0,8)};AutomationProperties.SetName(fingerprint,"Full device fingerprint");details.Children.Add(fingerprint);string created="Not available";try{if(entry.CreatedUtc>0)created=DateTimeOffset.FromUnixTimeSeconds(entry.CreatedUtc).LocalDateTime.ToString("g");}catch(ArgumentOutOfRangeException){}details.Children.Add(HistoryText("Enrolled: "+created+"\nLast verified activity: Not retained\nAccount: "+snapshot.Account+"\nHistory permissions: managed separately in Privacy & Security"));content.Children.Add(new Expander{Header="Identity and authorization details",Content=details,Foreground=Brush("#A1AEBB"),Margin=new Thickness(0,8,0,4)});
                var actions=new WrapPanel();content.Children.Add(actions);var rename=HistoryButton("Rename",()=>ManageDeviceName(dialog,snapshot,entry));rename.IsEnabled=!managedDeviceBusy&&entry.State=="active";actions.Children.Add(rename);var revoke=HistoryButton("Revoke device",()=>RevokeManagedDevice(dialog,snapshot,entry));revoke.IsEnabled=!managedDeviceBusy&&snapshot.CanRevoke(entry);revoke.ToolTip=entry.Current?"This device cannot revoke itself.":entry.State=="revoked"?"Revoked devices cannot automatically re-enroll.":"Require account password and this device's signed proof.";actions.Children.Add(revoke);
                if(!entry.Current&&entry.State=="active"){
                    DeviceIdentitySnapshot ownEvidence;string authority="";if(identitySnapshots.TryGetValue(snapshot.Account,out ownEvidence)&&ownEvidence.AuthorityProof.Length!=0&&ownEvidence.AuthorityActive)try{authority=ownEvidence.AuthorityFingerprint();}catch(InvalidDataException){}
                    var enroll=HistoryButton("Endorse for device identity",()=>ConfirmExactIdentity(dialog,"Endorse device identity","Approve this registered device as another identity for account "+snapshot.Account+". This does not grant history, group or cache access.",entry.Identity,typed=>RunIdentityAction(client=>client.Endorse(entry.Identity,typed))));
                    enroll.IsEnabled=!identityBusy&&authority==model.OwnId;actions.Children.Add(enroll);
                }
                rows.Children.Add(new Border{Child=content,BorderBrush=Brush("#2B3946"),BorderThickness=new Thickness(1),CornerRadius=new CornerRadius(12),Margin=new Thickness(0,0,0,12)});
            }
        };
        EventHandler changed=(s,e)=>render();ManagedDevicesChanged+=changed;IdentityDirectoryChanged+=changed;dialog.Closed+=(s,e)=>{ManagedDevicesChanged-=changed;IdentityDirectoryChanged-=changed;};panel.IsVisibleChanged+=(s,e)=>{if(panel.IsVisible){render();managedDeviceNextUtc=DateTime.MinValue;identityNextUtc=DateTime.MinValue;PumpDeviceManagement();PumpDeviceIdentity();}};render();return panel;
    }
    void ManageDeviceName(Window owner,AccountDeviceSnapshot snapshot,ManagedAccountDevice device){
        var body=new StackPanel{Margin=new Thickness(20)};var dialog=new Window{Title="Rename device",Owner=owner,Width=480,SizeToContent=SizeToContent.Height,ResizeMode=ResizeMode.NoResize,Background=owner.Background,Foreground=owner.Foreground,Resources=owner.Resources,WindowStartupLocation=WindowStartupLocation.CenterOwner,Content=body};body.Children.Add(HistoryText("Shared label for account "+snapshot.Account+"\n"+device.Identity));var name=new TextBox{Text=device.Name,MaxLength=64,Margin=new Thickness(0,12,0,12)};AutomationProperties.SetName(name,"Device display name");body.Children.Add(name);var message=HistoryText("");body.Children.Add(message);var save=HistoryButton("Save name",async()=>{try{string value=AccountDeviceSnapshot.NormalizeName(name.Text);dialog.IsEnabled=false;bool ok=await RunDeviceManagement(client=>client.Rename(snapshot,device.Identity,value),false);if(ok)dialog.Close();else message.Text=managedDeviceStatus;}catch(Exception error){message.Text=error.Message;}finally{dialog.IsEnabled=true;}});body.Children.Add(save);dialog.ShowDialog();
    }
    void RevokeManagedDevice(Window owner,AccountDeviceSnapshot snapshot,ManagedAccountDevice device){
        if(!snapshot.CanRevoke(device))return;var body=new StackPanel{Margin=new Thickness(20)};var dialog=new Window{Title="Revoke device access",Owner=owner,Width=540,SizeToContent=SizeToContent.Height,ResizeMode=ResizeMode.NoResize,Background=owner.Background,Foreground=owner.Foreground,Resources=owner.Resources,WindowStartupLocation=WindowStartupLocation.CenterOwner,Content=body};body.Children.Add(HistoryTitle("Revoke "+device.DisplayName+"?"));body.Children.Add(HistoryText("Account "+snapshot.Account+"\nExact device fingerprint:\n"+device.Identity+"\n\nThis stops future authorized messaging, history, cache and group access for this device. Other legacy account sessions may need to reconnect. Existing keys, local history and queued messages are preserved. This action cannot be undone here."));body.Children.Add(HistoryText("Confirm your account password"));var password=new PasswordBox{Margin=new Thickness(0,8,0,12),MaxLength=256};AutomationProperties.SetName(password,"Account password for device revocation");body.Children.Add(password);var message=HistoryText("");body.Children.Add(message);var actions=new WrapPanel();body.Children.Add(actions);actions.Children.Add(HistoryButton("Cancel",()=>dialog.Close()));actions.Children.Add(HistoryButton("Confirm revocation",async()=>{string secret=password.Password;password.Clear();if(String.IsNullOrEmpty(secret)){message.Text="Enter your account password.";return;}dialog.IsEnabled=false;try{bool ok=await RunDeviceManagement(client=>client.Revoke(snapshot,device.Identity,secret),false);if(ok)dialog.Close();else message.Text=managedDeviceStatus;}finally{secret=null;dialog.IsEnabled=true;}}));dialog.Closed+=(s,e)=>password.Clear();dialog.ShowDialog();
    }
}
}
