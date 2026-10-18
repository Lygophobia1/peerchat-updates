using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace PeerChatDesktop {
// Supplemental outgoing UI preference, not recipient authorization or transfer state.
public sealed class AttachmentSendPreferenceStore {
    readonly string path;
    readonly byte[] entropy;
    public AttachmentSendPreferenceStore(string profileDirectory){
        var profile=Path.GetFullPath(profileDirectory);
        path=Path.Combine(profile,"attachment-send-preference.dat");
        entropy=Encoding.UTF8.GetBytes("PeerChat/attachment-send-preference/v1\0"+profile.ToUpperInvariant());
    }
    public DmAttachmentDeliveryMode Load(){
        try{
            if(!File.Exists(path))return DmAttachmentDeliveryMode.OnlineDevice;
            var value=Encoding.UTF8.GetString(ProtectedData.Unprotect(File.ReadAllBytes(path),entropy,DataProtectionScope.CurrentUser));
            return value=="1|cached"?DmAttachmentDeliveryMode.EncryptedCache:DmAttachmentDeliveryMode.OnlineDevice;
        }catch{return DmAttachmentDeliveryMode.OnlineDevice;}
    }
    public void Save(DmAttachmentDeliveryMode mode){
        if(mode!=DmAttachmentDeliveryMode.OnlineDevice&&mode!=DmAttachmentDeliveryMode.EncryptedCache)throw new ArgumentException("Select Direct or Cached.");
        Directory.CreateDirectory(Path.GetDirectoryName(path));
        var temp=path+"."+Guid.NewGuid().ToString("N")+".tmp";
        try{
            var bytes=Encoding.UTF8.GetBytes(mode==DmAttachmentDeliveryMode.EncryptedCache?"1|cached":"1|direct");
            File.WriteAllBytes(temp,ProtectedData.Protect(bytes,entropy,DataProtectionScope.CurrentUser));
            if(File.Exists(path))File.Replace(temp,path,null);else File.Move(temp,path);
        }finally{if(File.Exists(temp))File.Delete(temp);}
    }
}
}
