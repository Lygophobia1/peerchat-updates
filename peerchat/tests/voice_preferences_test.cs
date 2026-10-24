using System;
using System.IO;
using System.Text;
using System.Web.Script.Serialization;
using PeerChatDesktop;

namespace PeerChatDesktop {
    public sealed class VoiceDevice { public int id; public string name; }
    public sealed class VoiceDevices { public VoiceDevice[] inputs; public VoiceDevice[] outputs; }
}

static class VoicePreferencesTest {
    static void Require(bool value,string message){if(!value)throw new InvalidOperationException(message);}
    static int Main(){
        string root=Path.Combine(Path.GetTempPath(),"peerchat-voice-prefs-"+Guid.NewGuid().ToString("N"));
        string profileA=Path.Combine(root,"A"),profileB=Path.Combine(root,"B");
        Func<byte[],byte[]> identity=data=>(byte[])data.Clone();
        try {
            var screen=new ScreenSharePreferenceStore(profileA);
            Require(screen.Load().Backend=="WebRTC Compatibility","Missing screen preference did not default to Compatibility");
            Directory.CreateDirectory(profileA);
            string screenPath=Path.Combine(profileA,"screen-share-preferences.json");
            File.WriteAllText(screenPath,"{\"Version\":1,\"FramesPerSecond\":60,\"BitrateBitsPerSecond\":16000000,\"CaptureCursor\":false}");
            var missing=screen.Load();Require(missing.Backend=="WebRTC Compatibility"&&missing.FramesPerSecond==60&&missing.BitrateBitsPerSecond==16000000&&!missing.CaptureCursor,"Missing backend default changed saved quality");
            foreach(string backend in new[]{"Native Windows","WebRTC Compatibility"}){
                missing.Backend=backend;screen.Save(missing);
                for(int i=0;i<16;i++)Require(new ScreenSharePreferenceStore(profileA).Load().Backend==backend,"Explicit backend selection did not survive reopen");
            }
            var storeA=new VoicePreferenceStore(profileA,identity,identity);
            var defaults=storeA.Load();
            Require(defaults.EchoCancellationEnabled,"Fresh profile AEC must default ON");
            Require(defaults.NoiseSuppressionEnabled&&defaults.AutomaticGainEnabled&&defaults.NoiseSuppressionStrength==1&&defaults.AutomaticGainStrength==1,"Voice preset defaults are not enabled Balanced/Balanced");
            Require(String.Join("|",VoiceProcessingPresets.NoiseSuppressionNames)=="Low|Balanced|High"&&String.Join("|",VoiceProcessingPresets.AutomaticGainNames)=="Gentle|Balanced|Strong","Friendly strength choices changed");
            Require(defaults.AutomaticNoiseControlLevel==1,"Automatic level must default to Medium");
            foreach(int level in new[]{0,1,2}){defaults.AutomaticNoiseControlLevel=level;storeA.Save(defaults);Require(storeA.Load().AutomaticNoiseControlLevel==level,"Automatic level did not persist");}
            Require(new VoicePreferenceStore(profileB,identity,identity).Load().AutomaticNoiseControlLevel==1,"Automatic level leaked to another profile");
            defaults.AutomaticNoiseControlLevel=3;bool invalidAuto=false;try{storeA.Save(defaults);}catch(InvalidOperationException){invalidAuto=true;}Require(invalidAuto,"Invalid automatic level accepted");defaults.AutomaticNoiseControlLevel=2;
            defaults.NoiseSuppressionEnabled=false;defaults.NoiseSuppressionStrength=2;
            defaults.AutomaticGainEnabled=false;defaults.AutomaticGainStrength=0;
            defaults.InputVolume=73;defaults.OutputVolume=125;defaults.EchoCancellationEnabled=false;
            storeA.Save(defaults);
            var loaded=new VoicePreferenceStore(profileA,identity,identity).Load();
            Require(!loaded.NoiseSuppressionEnabled&&loaded.NoiseSuppressionStrength==2&&!loaded.AutomaticGainEnabled&&loaded.AutomaticGainStrength==0,"Independent NS/AGC toggles or presets did not persist");
            Require(!loaded.EchoCancellationEnabled,"Explicit AEC OFF did not persist");Require(loaded.InputVolume==73&&loaded.OutputVolume==125,"Existing input/output volume preferences changed");
            loaded.NoiseSuppressionEnabled=true;loaded.AutomaticGainEnabled=true;storeA.Save(loaded);
            var restored=storeA.Load();
            Require(restored.NoiseSuppressionEnabled&&restored.NoiseSuppressionStrength==2&&restored.AutomaticGainEnabled&&restored.AutomaticGainStrength==0,"Re-enabling discarded the saved strength");
            var storeB=new VoicePreferenceStore(profileB,identity,identity);var defaultsB=storeB.Load();
            Require(defaultsB.NoiseSuppressionEnabled&&defaultsB.NoiseSuppressionStrength==1&&defaultsB.AutomaticGainEnabled&&defaultsB.AutomaticGainStrength==1,"Voice presets were not profile-local");
            bool rejected=false;loaded.NoiseSuppressionStrength=3;try{storeA.Save(loaded);}catch(InvalidOperationException){rejected=true;}
            Require(rejected,"Out-of-range voice preset was accepted");

            Directory.CreateDirectory(profileB);
            var json=new JavaScriptSerializer();
            var older=new {Version=1,InputDeviceId=-1,OutputDeviceId=-1,InputDeviceName="Windows default microphone",OutputDeviceName="Windows default output",InputVolume=100,OutputVolume=100,AudioProcessingEnabled=true,NoiseSuppressionEnabled=true,AutomaticGainEnabled=true};
            File.WriteAllBytes(Path.Combine(profileB,"voice-preferences.dat"),Encoding.UTF8.GetBytes(json.Serialize(older)));
            var upgraded=storeB.Load();
            Require(upgraded.AutomaticNoiseControlLevel==1,"Legacy automatic level must default Medium");Require(upgraded.EchoCancellationEnabled,"Missing AEC field must default ON");Require(upgraded.NoiseSuppressionStrength==1&&upgraded.AutomaticGainStrength==1,"Existing profile without preset fields did not receive Balanced/Balanced defaults");
            Console.WriteLine("PASS: voice preset defaults, profile-local persistence, independent NS/AGC toggles, legacy defaults, and volume preservation.");
            return 0;
        } catch(Exception error){Console.Error.WriteLine("FAIL: "+error);return 1;}
    }
}
