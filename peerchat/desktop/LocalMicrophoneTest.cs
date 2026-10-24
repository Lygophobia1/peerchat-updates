using System;
using System.Runtime.InteropServices;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
namespace PeerChatDesktop {
public sealed class MicrophoneTestState {public bool recording,available,processing;public int frames,played,playing;public float rawRms,processedRms;public long rawFullScale,processedFullScale;public string error;}
public sealed partial class CoreAdapter {
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)]static extern IntPtr ui_microphone_test_v3(IntPtr h,int operation,int input,int output,int volume,int enabled,int ns,int agc,uint noise,uint gain,int automatic,uint level);
    public MicrophoneTestState MicrophoneTest(int operation,VoicePreferenceData preferences=null){if(handle==IntPtr.Zero)return new MicrophoneTestState{error="Local backend is closed."};var p=preferences??new VoicePreferenceData();try{return new JavaScriptSerializer().Deserialize<MicrophoneTestState>(Marshal.PtrToStringUni(ui_microphone_test_v3(handle,operation,p.InputDeviceId,p.OutputDeviceId,p.InputVolume,p.AudioProcessingEnabled?1:0,p.NoiseSuppressionEnabled?1:0,p.AutomaticGainEnabled?1:0,(uint)p.NoiseSuppressionStrength,(uint)p.AutomaticGainStrength,p.AutomaticNoiseControlEnabled?1:0,(uint)p.AutomaticNoiseControlLevel)));}catch(EntryPointNotFoundException){return new MicrophoneTestState{error="Local microphone comparison is unavailable in this runtime."};}}
}
public partial class DesktopApplication {
    bool localMicrophoneTestOpen;
    void ShowLocalMicrophoneTest(){
        if(localMicrophoneTestOpen||model.VoiceStateCode!=0||(groupVoice!=null&&(groupVoice.Snapshot().Joined||groupVoice.Snapshot().State=="Joining"))){model.Status="Leave voice before running a local microphone comparison.";Refresh();return;}
        localMicrophoneTestOpen=true;var profile=profileSessionGeneration;
        var panel=new StackPanel{Margin=new Thickness(20)};
        panel.Children.Add(new TextBlock{Text="Local microphone comparison",FontSize=22});
        panel.Children.Add(new TextBlock{Text="Record up to 8 seconds in memory, then listen to raw or processed audio. Nothing is uploaded or saved. Playback is 50% gain. Use headphones. Raw includes device/OS processing and input volume. AEC/network quality is not tested here.",TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,12,0,12)});
        var readout=new TextBlock{TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,12,0,0)};var buttons=new WrapPanel();panel.Children.Add(buttons);panel.Children.Add(readout);
        var owner=Application.Current==null?window:System.Linq.Enumerable.LastOrDefault(System.Linq.Enumerable.OfType<Window>(Application.Current.Windows),w=>w.IsActive)??window;
        var dialog=new Window{Title="Microphone comparison — local only",Owner=owner,Width=600,Height=350,MinWidth=430,MinHeight=300,Background=Brush("#111920"),Foreground=Brush("#EDF1F5"),Content=panel,Resources=window.Resources};
        Action<MicrophoneTestState> show=s=>readout.Text=(s.recording?"Recording":s.playing==1?"Playing raw":s.playing==2?"Playing processed":s.available?"Ready to compare":"Stopped")+" · "+(s.frames*.02).ToString("0.00")+" s\nRaw RMS: "+s.rawRms.ToString("0.000")+" · Processed RMS: "+s.processedRms.ToString("0.000")+"\nFull-scale samples (not a measured distortion score): raw "+s.rawFullScale+", processed "+s.processedFullScale+"\n"+(s.processing?"Existing NS/AGC settings captured when recording started.":"Processing bypassed.")+"\n"+(s.error??"");
        DateTime recordUntil=DateTime.MaxValue;
        foreach(var item in new[]{Tuple.Create("Record 8 seconds",1),Tuple.Create("Stop recording",2),Tuple.Create("Listen raw",3),Tuple.Create("Listen processed",4),Tuple.Create("Clear",5)}){var choice=item;var button=new Button{Content=item.Item1,Padding=new Thickness(9,6,9,6),Margin=new Thickness(0,0,6,6)};button.Click+=(s,e)=>{recordUntil=choice.Item2==1?DateTime.UtcNow.AddSeconds(8):DateTime.MaxValue;show(core.MicrophoneTest(choice.Item2,voicePreferences));};buttons.Children.Add(button);}
        var timer=new DispatcherTimer{Interval=TimeSpan.FromMilliseconds(250)};timer.Tick+=(s,e)=>{if(profile!=profileSessionGeneration||model.VoiceStateCode!=0){dialog.Close();return;}var state=core.MicrophoneTest(0);if(state.recording&&DateTime.UtcNow>=recordUntil){state=core.MicrophoneTest(2);recordUntil=DateTime.MaxValue;}show(state);};
        dialog.Closed+=(s,e)=>{timer.Stop();core.MicrophoneTest(5);localMicrophoneTestOpen=false;};
        show(core.MicrophoneTest(0));timer.Start();try{dialog.ShowDialog();}finally{timer.Stop();core.MicrophoneTest(5);localMicrophoneTestOpen=false;}
    }
}
}
