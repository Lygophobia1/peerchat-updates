using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;

namespace PeerChatDesktop {
public sealed class ConnectionPreferenceData { public int Version=1; public bool AllowDirectP2p=false; }
public sealed class ConnectionPreferenceStore {
    readonly string path;readonly JavaScriptSerializer json=new JavaScriptSerializer();
    public ConnectionPreferenceStore(string profileDirectory){path=Path.Combine(profileDirectory,"connection-preferences.dat");}
    public ConnectionPreferenceData Load(){try{if(!File.Exists(path))return new ConnectionPreferenceData();var value=json.Deserialize<ConnectionPreferenceData>(Encoding.UTF8.GetString(ProtectedData.Unprotect(File.ReadAllBytes(path),null,DataProtectionScope.CurrentUser)));return value!=null&&value.Version==1?value:new ConnectionPreferenceData();}catch{return new ConnectionPreferenceData();}}
    public void Save(ConnectionPreferenceData value){if(value==null||value.Version!=1)throw new InvalidOperationException("Connection preferences are invalid.");Directory.CreateDirectory(Path.GetDirectoryName(path));string temp=path+"."+Guid.NewGuid().ToString("N")+".tmp";File.WriteAllBytes(temp,ProtectedData.Protect(Encoding.UTF8.GetBytes(json.Serialize(value)),null,DataProtectionScope.CurrentUser));if(File.Exists(path))File.Replace(temp,path,null);else File.Move(temp,path);}
}
public sealed class VoicePreferenceData {
    public int Version=1;
    public int InputDeviceId=-1,OutputDeviceId=-1;
    public string InputDeviceName="Windows default microphone",OutputDeviceName="Windows default output";
    public int InputVolume=100,OutputVolume=100;
    public bool AudioProcessingEnabled=true,NoiseSuppressionEnabled=true,AutomaticGainEnabled=true;
    public bool EchoCancellationEnabled=true; // Missing field defaults ON; an explicit saved false is preserved.
    public int NoiseSuppressionStrength=1,AutomaticGainStrength=1;
}

public static class VoiceProcessingPresets {
    public static readonly string[] NoiseSuppressionNames={"Low","Balanced","High"};
    public static readonly string[] AutomaticGainNames={"Gentle","Balanced","Strong"};
    public static string NoiseSuppressionName(int value){return value>=0&&value<NoiseSuppressionNames.Length?NoiseSuppressionNames[value]:"Balanced";}
    public static string AutomaticGainName(int value){return value>=0&&value<AutomaticGainNames.Length?AutomaticGainNames[value]:"Balanced";}
}

// Profile preference only. No ephemeral call ID/state or audio content is stored.
public sealed class VoicePreferenceStore {
    readonly string path;
    readonly JavaScriptSerializer json=new JavaScriptSerializer();
    readonly Func<byte[],byte[]> protect,unprotect;
    public VoicePreferenceStore(string profileDirectory):this(profileDirectory,data=>ProtectedData.Protect(data,null,DataProtectionScope.CurrentUser),data=>ProtectedData.Unprotect(data,null,DataProtectionScope.CurrentUser)){}
    internal VoicePreferenceStore(string profileDirectory,Func<byte[],byte[]> protectData,Func<byte[],byte[]> unprotectData){path=Path.Combine(profileDirectory,"voice-preferences.dat");protect=protectData;unprotect=unprotectData;}
    public VoicePreferenceData Load(){try{if(!File.Exists(path))return new VoicePreferenceData();var value=json.Deserialize<VoicePreferenceData>(Encoding.UTF8.GetString(unprotect(File.ReadAllBytes(path))));return Valid(value)?value:new VoicePreferenceData();}catch{return new VoicePreferenceData();}}
    public void Save(VoicePreferenceData value){if(!Valid(value))throw new InvalidOperationException("Voice preferences are invalid.");Directory.CreateDirectory(Path.GetDirectoryName(path));string temporary=path+"."+Guid.NewGuid().ToString("N")+".tmp";File.WriteAllBytes(temporary,protect(Encoding.UTF8.GetBytes(json.Serialize(value))));if(File.Exists(path))File.Replace(temporary,path,null);else File.Move(temporary,path);}
    public static void ResolveDevices(VoicePreferenceData value,VoiceDevices devices){if(value==null||devices==null)return;var inputs=devices.inputs??new VoiceDevice[0];var outputs=devices.outputs??new VoiceDevice[0];if(!inputs.Any(item=>item.id==value.InputDeviceId&&item.name==value.InputDeviceName)){value.InputDeviceId=-1;value.InputDeviceName="Windows default microphone";}if(!outputs.Any(item=>item.id==value.OutputDeviceId&&item.name==value.OutputDeviceName)){value.OutputDeviceId=-1;value.OutputDeviceName="Windows default output";}}
    static bool Valid(VoicePreferenceData value){return value!=null&&value.Version==1&&value.InputDeviceId>=-1&&value.OutputDeviceId>=-1&&value.InputVolume>=0&&value.InputVolume<=200&&value.OutputVolume>=0&&value.OutputVolume<=200&&value.NoiseSuppressionStrength>=0&&value.NoiseSuppressionStrength<=2&&value.AutomaticGainStrength>=0&&value.AutomaticGainStrength<=2&&(value.InputDeviceName??"").Length<=256&&(value.OutputDeviceName??"").Length<=256;}
}

// Screen preferences are profile-scoped. Handles, share IDs, peers and live
// negotiation state are deliberately never persisted.
public sealed class ScreenSharePreferenceData {
    public int Version=1;
    public int FramesPerSecond=30;
    public int BitrateBitsPerSecond=8000000;
    public bool CaptureCursor=true;
    public string Backend="Native Windows";
}
public sealed class ScreenSharePreferenceStore {
    readonly string path;
    readonly JavaScriptSerializer json=new JavaScriptSerializer();
    public ScreenSharePreferenceStore(string profileDirectory){path=Path.Combine(profileDirectory,"screen-share-preferences.json");}
    public ScreenSharePreferenceData Load(){
        try{if(!File.Exists(path))return new ScreenSharePreferenceData();var value=json.Deserialize<ScreenSharePreferenceData>(File.ReadAllText(path,Encoding.UTF8));return Valid(value)?value:new ScreenSharePreferenceData();}
        catch{return new ScreenSharePreferenceData();}
    }
    public void Save(ScreenSharePreferenceData value){
        if(!Valid(value))throw new InvalidOperationException("Screen-share preferences are invalid.");
        Directory.CreateDirectory(Path.GetDirectoryName(path));
        string temporary=path+"."+Guid.NewGuid().ToString("N")+".tmp";
        File.WriteAllText(temporary,json.Serialize(value),new UTF8Encoding(false));
        if(File.Exists(path))File.Replace(temporary,path,null);else File.Move(temporary,path);
    }
    static bool Valid(ScreenSharePreferenceData value){return value!=null&&value.Version==1&&
        (value.Backend=="Native Windows"||value.Backend=="WebRTC Compatibility")&&
        (value.FramesPerSecond==15||value.FramesPerSecond==30||value.FramesPerSecond==60)&&
        new[]{2000000,4000000,8000000,16000000,25000000}.Contains(value.BitrateBitsPerSecond);}
}
}
