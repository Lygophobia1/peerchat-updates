using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;

namespace PeerChatDesktop {
// This transport is available only on a pinned, authenticated device RPC session.
// A directory response is public evidence, never authority to replace a contact pin.
public sealed partial class DiscoveryClient {
    internal Dictionary<string,object> DeviceIdentityRequest(string operation,string device,Dictionary<string,object> fields,long account,string token,string pin){
        if(!new[]{"device_identity_register","device_identity_snapshot","device_identity_authority_challenge","device_identity_authority_commit","device_identity_endorse_challenge","device_identity_endorse_commit"}.Contains(operation)||
           !supportsRpc||account<=0||AccountId!=account||SessionToken!=token||CertificateSha256!=pin||!MatchesAttachmentDeviceSession(account,device))
            throw new InvalidOperationException("Device identity requires the current verified device session.");
        var body=Rpc(operation,device,fields??new Dictionary<string,object>(),token,pin,account);
        if(!body.ContainsKey("ok")||!Boolean.Equals(body["ok"],true)){
            object error;body.TryGetValue("error",out error);throw new DiscoveryRequestException(error as string??"device_identity_rejected");
        }
        if(AccountId!=account||SessionToken!=token||CertificateSha256!=pin)throw new InvalidOperationException("Device identity session changed.");
        return body;
    }
}

internal sealed class DeviceIdentitySnapshot {
    internal long Account,Revision;
    internal long RequestedUtcMilliseconds;
    internal string Server;
    internal byte[] AuthorityProof;
    internal bool AuthorityActive;
    internal KeyValuePair<byte[],bool>[] Endorsements;
    internal byte[][] PossessionProofs;
    internal bool HasPossession(string device){
        foreach(byte[] wire in PossessionProofs){
            var signed=new EvidenceReader(wire,"PeerChat/account-device-possession-signed/v1");byte[] details=signed.Field(768);signed.Field(64);signed.End();
            var record=new EvidenceReader(details,"PeerChat/account-device-possession/v1");string server=record.Text(64);long account=record.Number();string owner=record.Text(64);record.Field(72);record.End();
            if(server!=Server||account!=Account||!DeviceIdentityDirectoryClient.Hex(owner))throw new InvalidDataException("Possession evidence does not match this account or server.");
            if(owner==device)return true;
        }
        return false;
    }
    // Display only. The native verifier checks the signatures, exact key hashes,
    // current authenticated mapping and confirmed fingerprint before bootstrap.
    internal string AuthorityFingerprint(){
        if(AuthorityProof==null||AuthorityProof.Length==0)return "";
        var signed=new EvidenceReader(AuthorityProof,"PeerChat/account-device-authority-signed/v1");byte[] payload=signed.Field(1792);signed.Field(64);signed.End();
        var authority=new EvidenceReader(payload,"PeerChat/account-device-authority/v1");byte[] possession=authority.Field(1024);
        var possessionSigned=new EvidenceReader(possession,"PeerChat/account-device-possession-signed/v1");byte[] details=possessionSigned.Field(768);possessionSigned.Field(64);possessionSigned.End();
        var record=new EvidenceReader(details,"PeerChat/account-device-possession/v1");string server=record.Text(64);long account=record.Number();string device=record.Text(64);record.Field(72);record.End();
        if(server!=Server||account!=Account||!DeviceIdentityDirectoryClient.Hex(device))throw new InvalidDataException("Authority evidence does not match this account or server.");
        return device;
    }
    internal static DeviceIdentitySnapshot Parse(Dictionary<string,object> row,long account,string pin,bool own){
        if(row==null||account<=0||!DeviceIdentityDirectoryClient.Hex(pin)||DeviceIdentityDirectoryClient.Number(row,"deviceIdentityVersion")!=1||
           DeviceIdentityDirectoryClient.Number(row,"accountId")!=account||DeviceIdentityDirectoryClient.Text(row,"serverFingerprint")!=pin)
            throw new InvalidDataException("Device identity snapshot binding was rejected.");
        object active;if(!row.TryGetValue("authorityActive",out active)||!(active is bool))throw new InvalidDataException("Invalid authority state.");
        string authority=DeviceIdentityDirectoryClient.Text(row,"authorityProof");
        if(authority==null)throw new InvalidDataException("Authority evidence is missing.");
        byte[] authorityProof=DeviceIdentityDirectoryClient.Decode(authority,2048,true);
        if(authorityProof.Length==0&&(bool)active)throw new InvalidDataException("Active authority has no proof.");
        object raw;if(!row.TryGetValue("endorsements",out raw)||!(raw is IEnumerable)||raw is string)throw new InvalidDataException("Endorsement list is missing.");
        var endorsements=new List<KeyValuePair<byte[],bool>>();foreach(object value in (IEnumerable)raw){
            var entry=value as Dictionary<string,object>;object flag;if(entry==null||endorsements.Count>=32||!entry.TryGetValue("active",out flag)||!(flag is bool))throw new InvalidDataException("Invalid endorsement entry.");
            endorsements.Add(new KeyValuePair<byte[],bool>(DeviceIdentityDirectoryClient.Decode(DeviceIdentityDirectoryClient.Text(entry,"proof"),2048,false),(bool)flag));
        }
        if(!row.TryGetValue("possessionProofs",out raw)||!(raw is IEnumerable)||raw is string)throw new InvalidDataException("Possession list is missing.");
        var possessions=new List<byte[]>();foreach(object value in (IEnumerable)raw){if(possessions.Count>=32||!(value is string))throw new InvalidDataException("Invalid possession entry.");possessions.Add(DeviceIdentityDirectoryClient.Decode((string)value,1024,false));}
        if(!own&&possessions.Count!=0)throw new InvalidDataException("Contact snapshot disclosed private account inventory.");
        return new DeviceIdentitySnapshot{Account=account,Revision=DeviceIdentityDirectoryClient.Number(row,"revision"),Server=pin,AuthorityProof=authorityProof,AuthorityActive=(bool)active,Endorsements=endorsements.ToArray(),PossessionProofs=possessions.ToArray()};
    }
}

internal sealed class EvidenceReader {
    readonly byte[] wire;int offset;
    internal EvidenceReader(byte[] value,string domain){wire=value??new byte[0];byte[] prefix=Encoding.ASCII.GetBytes(domain+"\0");if(wire.Length<prefix.Length||!wire.Take(prefix.Length).SequenceEqual(prefix))throw new InvalidDataException("Identity evidence domain mismatch.");offset=prefix.Length;}
    int U32(){if(wire.Length-offset<4)throw new InvalidDataException("Identity evidence truncated.");int value=0;for(int n=0;n<4;n++)value=(value<<8)|wire[offset++];if(value<0)throw new InvalidDataException("Identity evidence size invalid.");return value;}
    internal long Number(){if(wire.Length-offset<8)throw new InvalidDataException("Identity evidence truncated.");ulong value=0;for(int n=0;n<8;n++)value=(value<<8)|wire[offset++];if(value>Int64.MaxValue)throw new InvalidDataException("Identity evidence number invalid.");return (long)value;}
    internal byte[] Field(int maximum){int count=U32();if(count>maximum||count>wire.Length-offset)throw new InvalidDataException("Identity evidence field invalid.");var value=new byte[count];Buffer.BlockCopy(wire,offset,value,0,count);offset+=count;return value;}
    internal string Text(int maximum){return new UTF8Encoding(false,true).GetString(Field(maximum));}
    internal void End(){if(offset!=wire.Length)throw new InvalidDataException("Identity evidence trailing bytes.");}
}

internal sealed class DeviceIdentityDirectoryClient {
    const string Command="PeerChat/IdentityDirectoryCommand/v1";
    readonly DiscoveryClient client;
    readonly long localAccount,generation;
    readonly string localDevice,token,pin,transportPin;
    readonly Func<byte[],byte[]> native;
    readonly Func<bool> current;
    internal DeviceIdentityDirectoryClient(DiscoveryClient client,long account,string device,long friendGeneration,Func<byte[],byte[]> native,Func<bool> current){
        if(client==null||account<=0||friendGeneration<=0||!Hex(device)||!Hex((client.CertificateSha256??"").ToLowerInvariant()))throw new InvalidOperationException("Current authenticated identity context is unavailable.");
        this.client=client;localAccount=account;localDevice=device;generation=friendGeneration;token=client.SessionToken;transportPin=client.CertificateSha256;pin=transportPin.ToLowerInvariant();this.native=native;this.current=current;Check();
    }
    internal static bool Hex(string value){return value!=null&&value.Length==64&&value.All(c=>c>='0'&&c<='9'||c>='a'&&c<='f');}
    internal static string Text(Dictionary<string,object> row,string key){object value;return row.TryGetValue(key,out value)?value as string:null;}
    internal static long Number(Dictionary<string,object> row,string key){object value;long parsed;if(!row.TryGetValue(key,out value)||!Int64.TryParse(Convert.ToString(value,CultureInfo.InvariantCulture),NumberStyles.None,CultureInfo.InvariantCulture,out parsed)||parsed<0)throw new InvalidDataException("Invalid device identity number.");return parsed;}
    internal static byte[] Decode(string encoded,int maximum,bool empty){
        if(encoded==null||encoded.Length>((maximum+2)/3)*4)throw new InvalidDataException("Invalid identity proof size.");
        byte[] value;try{value=Convert.FromBase64String(encoded);}catch(FormatException){throw new InvalidDataException("Invalid identity proof encoding.");}
        if(value.Length>maximum||(!empty&&value.Length==0))throw new InvalidDataException("Invalid identity proof size.");return value;
    }
    void Check(){if(!client.MatchesAttachmentDeviceSession(localAccount,localDevice)||client.SessionToken!=token||client.CertificateSha256!=transportPin||current!=null&&!current())throw new InvalidOperationException("Device identity context changed.");}
    Dictionary<string,object> Send(string operation,Dictionary<string,object> fields){Check();var result=client.DeviceIdentityRequest(operation,localDevice,fields,localAccount,token,transportPin);Check();return result;}
    // protocol::Writer (PT type 30) uses little-endian u64 for numbers and lengths.
    // The nested signed evidence uses the separate mailbox big-endian codec.
    static void U64(Stream output,long value){if(value<0)throw new InvalidDataException("Invalid identity command number.");for(int shift=0;shift<=56;shift+=8)output.WriteByte((byte)((ulong)value>>shift));}
    static void Blob(Stream output,byte[] value){if(value==null||value.Length>65536)throw new InvalidDataException("Invalid identity command field.");U64(output,value.Length);output.Write(value,0,value.Length);}
    static void String(Stream output,string value){Blob(output,new UTF8Encoding(false,true).GetBytes(value??""));}
    byte[] Native(int operation,Action<Stream> fields,long requestStarted=0){
        Check();using(var output=new MemoryStream()){
            long now=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();if(requestStarted==0)requestStarted=now;
            if(requestStarted>now||now-requestStarted>30000)throw new InvalidDataException("Identity evidence is stale. Refresh the directory.");
            output.WriteByte(80);output.WriteByte(84);output.WriteByte(1);output.WriteByte(30);String(output,Command);U64(output,operation);U64(output,localAccount);U64(output,generation);String(output,pin);U64(output,requestStarted);if(fields!=null)fields(output);
            if(output.Length>70000)throw new InvalidDataException("Identity command exceeds bound.");var answer=native(output.ToArray());Check();return answer;
        }
    }
    internal byte[] RegisterSelf(){byte[] proof=Native(0,null);if(proof==null||proof.Length==0||proof.Length>1024)throw new InvalidDataException("Native possession proof was rejected.");Send("device_identity_register",new Dictionary<string,object>{{"proof",Convert.ToBase64String(proof)}});return proof;}
    internal DeviceIdentitySnapshot Snapshot(long account){if(account<=0)throw new ArgumentOutOfRangeException("account");long started=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();var snapshot=DeviceIdentitySnapshot.Parse(Send("device_identity_snapshot",new Dictionary<string,object>{{"accountId",account.ToString(CultureInfo.InvariantCulture)}}),account,pin,account==localAccount);snapshot.RequestedUtcMilliseconds=started;return snapshot;}
    internal bool Apply(DeviceIdentitySnapshot snapshot,string confirmedRoot=""){
        if(snapshot==null||snapshot.Account<=0||snapshot.Server!=pin||snapshot.AuthorityProof.Length==0)return false;
        if(confirmedRoot.Length!=0&&!Hex(confirmedRoot))throw new InvalidDataException("Enter the complete authority fingerprint.");
        if(snapshot.RequestedUtcMilliseconds<=0)throw new InvalidDataException("Refresh identity evidence before applying it.");
        var result=Native(3,output=>{U64(output,snapshot.Account);U64(output,snapshot.Revision);Blob(output,snapshot.AuthorityProof);U64(output,snapshot.AuthorityActive?1:0);U64(output,snapshot.Endorsements.Length);foreach(var item in snapshot.Endorsements){Blob(output,item.Key);U64(output,item.Value?1:0);}String(output,confirmedRoot);},snapshot.RequestedUtcMilliseconds);
        if(result==null||result.Length!=1||result[0]!=1)throw new InvalidDataException("Native identity evidence was not accepted.");return true;
    }
    byte[] Challenge(string operation,Dictionary<string,object> fields){var row=Send(operation,fields);if(Number(row,"deviceIdentityVersion")!=1||Number(row,"accountId")!=localAccount||Text(row,"serverFingerprint")!=pin)throw new InvalidDataException("Identity challenge binding was rejected.");return Decode(Text(row,"payload"),1792,false);}
    internal DeviceIdentitySnapshot ChooseAuthority(string confirmedFingerprint){
        if(confirmedFingerprint!=localDevice)throw new InvalidDataException("Confirm this device's complete fingerprint.");
        RegisterSelf();var payload=Challenge("device_identity_authority_challenge",new Dictionary<string,object>());
        byte[] proof=Native(1,output=>{Blob(output,payload);String(output,confirmedFingerprint);});
        if(proof==null||proof.Length==0||proof.Length>2048)throw new InvalidDataException("Native authority proof was rejected.");
        Send("device_identity_authority_commit",new Dictionary<string,object>{{"proof",Convert.ToBase64String(proof)}});
        var snapshot=Snapshot(localAccount);Apply(snapshot,confirmedFingerprint);return snapshot;
    }
    internal DeviceIdentitySnapshot Endorse(string target,string confirmedFingerprint){
        if(!Hex(target)||confirmedFingerprint!=target||target==localDevice)throw new InvalidDataException("Confirm the target device's complete fingerprint.");
        var currentOwn=Snapshot(localAccount);if(!Apply(currentOwn))throw new InvalidDataException("Choose this device as enrollment authority first.");
        var payload=Challenge("device_identity_endorse_challenge",new Dictionary<string,object>{{"targetDevice",target}});
        byte[] proof=Native(2,output=>{Blob(output,payload);String(output,confirmedFingerprint);});
        if(proof==null||proof.Length==0||proof.Length>2048)throw new InvalidDataException("Native endorsement proof was rejected.");
        Send("device_identity_endorse_commit",new Dictionary<string,object>{{"proof",Convert.ToBase64String(proof)}});
        var snapshot=Snapshot(localAccount);Apply(snapshot);return snapshot;
    }
}
}
