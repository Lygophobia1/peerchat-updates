using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;

namespace PeerChatDesktop {
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
            if(opened!=null){managedDeviceSession=opened;opened=null;}managedDeviceOwner=owner;managedDeviceGeneration=generation;managedDeviceToken=token;managedDevices=result;managedDeviceStatus="Verified account "+account+" · revision "+result.Revision+" · updated "+DateTime.Now.ToString("t");managedDeviceNextUtc=DateTime.UtcNow.AddSeconds(60);return true;
        }catch(Exception error){if(ManagedContext(owner,generation,token,actor)){managedDeviceStatus=DeviceManagementClient.Failure(error);var stale=managedDeviceSession;managedDeviceSession=null;if(stale!=null)_=Task.Run(()=>stale.CloseRpc());managedDeviceNextUtc=DateTime.UtcNow.AddSeconds(60);}return false;}
        finally{if(opened!=null){var unused=opened;_=Task.Run(()=>unused.CloseRpc());}managedDeviceBusy=false;PublishManagedDevices();}
    }
    StackPanel DeviceManagerPanel(Window dialog){
        var panel=new StackPanel{Margin=new Thickness(4),MaxWidth=760,HorizontalAlignment=HorizontalAlignment.Stretch};panel.Children.Add(HistoryTitle("Device Manager"));panel.Children.Add(HistoryText("Manage verified devices belonging to this account only. Device names are labels, not fingerprints or trust decisions."));
        var status=HistoryText(managedDeviceStatus);panel.Children.Add(status);var buttons=new WrapPanel{Margin=new Thickness(0,12,0,12)};panel.Children.Add(buttons);
        var refresh=HistoryButton("Refresh",async()=>await RunDeviceManagement(null,false));var verify=HistoryButton("Verify this device",async()=>{if(Confirm("Verify this device","Use this profile's existing identity key to establish a device-bound management session? No keys, history, trust or grants will be replaced.","Verify"))await RunDeviceManagement(null,true);});buttons.Children.Add(refresh);buttons.Children.Add(verify);
        var rows=new StackPanel();panel.Children.Add(rows);panel.Children.Add(HistoryText("Revocation preserves local history and queued ciphertext. It blocks future authorized server access; it cannot erase data or keys already held by a device. Self/last-device revocation and reactivation are unavailable."));
        Action render=()=>{
            status.Text=managedDeviceStatus;refresh.IsEnabled=verify.IsEnabled=!managedDeviceBusy&&discovery!=null;
            rows.Children.Clear();var snapshot=managedDevices;if(snapshot==null||discovery==null||snapshot.Account!=discovery.AccountId||managedDeviceOwner!=discovery||managedDeviceGeneration!=discoverySessionGeneration){rows.Children.Add(HistoryText("No current verified inventory. Other accounts and historical contact identities are not listed here."));return;}
            foreach(var entry in snapshot.Devices.OrderByDescending(d=>d.Current).ThenBy(d=>d.State).ThenBy(d=>d.DisplayName,StringComparer.CurrentCultureIgnoreCase)){
                var content=new StackPanel{Margin=new Thickness(14)};var title=HistoryTitle(entry.DisplayName+(entry.Current?" · This device":""));title.FontSize=17;title.TextWrapping=TextWrapping.Wrap;content.Children.Add(title);content.Children.Add(HistoryText(entry.Identity.Substring(0,12)+"…"+entry.Identity.Substring(56)+" · "+entry.Status));content.Children.Add(HistoryText("Platform: "+entry.Platform+" · Version: "+entry.Version+"\nOffline attachments: "+entry.CacheDisplay+"\nEncrypted message capability: Not available"));
                var details=new StackPanel();var fingerprint=new TextBox{Text=entry.Identity,IsReadOnly=true,TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,6,0,8)};AutomationProperties.SetName(fingerprint,"Full device fingerprint");details.Children.Add(fingerprint);string created="Not available";try{if(entry.CreatedUtc>0)created=DateTimeOffset.FromUnixTimeSeconds(entry.CreatedUtc).LocalDateTime.ToString("g");}catch(ArgumentOutOfRangeException){}details.Children.Add(HistoryText("Enrolled: "+created+"\nLast verified activity: Not retained\nAccount: "+snapshot.Account+"\nHistory permissions: managed separately in Privacy & Security"));content.Children.Add(new Expander{Header="Identity and authorization details",Content=details,Foreground=Brush("#A1AEBB"),Margin=new Thickness(0,8,0,4)});
                var actions=new WrapPanel();content.Children.Add(actions);var rename=HistoryButton("Rename",()=>ManageDeviceName(dialog,snapshot,entry));rename.IsEnabled=!managedDeviceBusy&&entry.State=="active";actions.Children.Add(rename);var revoke=HistoryButton("Revoke device",()=>RevokeManagedDevice(dialog,snapshot,entry));revoke.IsEnabled=!managedDeviceBusy&&snapshot.CanRevoke(entry);revoke.ToolTip=entry.Current?"This device cannot revoke itself.":entry.State=="revoked"?"Revoked devices cannot automatically re-enroll.":"Require account password and this device's signed proof.";actions.Children.Add(revoke);
                rows.Children.Add(new Border{Child=content,BorderBrush=Brush("#2B3946"),BorderThickness=new Thickness(1),CornerRadius=new CornerRadius(12),Margin=new Thickness(0,0,0,12)});
            }
        };
        EventHandler changed=(s,e)=>render();ManagedDevicesChanged+=changed;dialog.Closed+=(s,e)=>ManagedDevicesChanged-=changed;panel.IsVisibleChanged+=(s,e)=>{if(panel.IsVisible){render();managedDeviceNextUtc=DateTime.MinValue;PumpDeviceManagement();}};render();return panel;
    }
    void ManageDeviceName(Window owner,AccountDeviceSnapshot snapshot,ManagedAccountDevice device){
        var body=new StackPanel{Margin=new Thickness(20)};var dialog=new Window{Title="Rename device",Owner=owner,Width=480,SizeToContent=SizeToContent.Height,ResizeMode=ResizeMode.NoResize,Background=owner.Background,Foreground=owner.Foreground,Resources=owner.Resources,WindowStartupLocation=WindowStartupLocation.CenterOwner,Content=body};body.Children.Add(HistoryText("Shared label for account "+snapshot.Account+"\n"+device.Identity));var name=new TextBox{Text=device.Name,MaxLength=64,Margin=new Thickness(0,12,0,12)};AutomationProperties.SetName(name,"Device display name");body.Children.Add(name);var message=HistoryText("");body.Children.Add(message);var save=HistoryButton("Save name",async()=>{try{string value=AccountDeviceSnapshot.NormalizeName(name.Text);dialog.IsEnabled=false;bool ok=await RunDeviceManagement(client=>client.Rename(snapshot,device.Identity,value),false);if(ok)dialog.Close();else message.Text=managedDeviceStatus;}catch(Exception error){message.Text=error.Message;}finally{dialog.IsEnabled=true;}});body.Children.Add(save);dialog.ShowDialog();
    }
    void RevokeManagedDevice(Window owner,AccountDeviceSnapshot snapshot,ManagedAccountDevice device){
        if(!snapshot.CanRevoke(device))return;var body=new StackPanel{Margin=new Thickness(20)};var dialog=new Window{Title="Revoke device access",Owner=owner,Width=540,SizeToContent=SizeToContent.Height,ResizeMode=ResizeMode.NoResize,Background=owner.Background,Foreground=owner.Foreground,Resources=owner.Resources,WindowStartupLocation=WindowStartupLocation.CenterOwner,Content=body};body.Children.Add(HistoryTitle("Revoke "+device.DisplayName+"?"));body.Children.Add(HistoryText("Account "+snapshot.Account+"\nExact device fingerprint:\n"+device.Identity+"\n\nThis stops future authorized messaging, history, cache and group access for this device. Other legacy account sessions may need to reconnect. Existing keys, local history and queued messages are preserved. This action cannot be undone here."));body.Children.Add(HistoryText("Confirm your account password"));var password=new PasswordBox{Margin=new Thickness(0,8,0,12),MaxLength=256};AutomationProperties.SetName(password,"Account password for device revocation");body.Children.Add(password);var message=HistoryText("");body.Children.Add(message);var actions=new WrapPanel();body.Children.Add(actions);actions.Children.Add(HistoryButton("Cancel",()=>dialog.Close()));actions.Children.Add(HistoryButton("Confirm revocation",async()=>{string secret=password.Password;password.Clear();if(String.IsNullOrEmpty(secret)){message.Text="Enter your account password.";return;}dialog.IsEnabled=false;try{bool ok=await RunDeviceManagement(client=>client.Revoke(snapshot,device.Identity,secret),false);if(ok)dialog.Close();else message.Text=managedDeviceStatus;}finally{secret=null;dialog.IsEnabled=true;}}));dialog.Closed+=(s,e)=>password.Clear();dialog.ShowDialog();
    }
}
}
