using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Serialization;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using PeerChatDesktop;

static class DeviceManagerUiTest {
    static readonly string Actor=new string('a',64),Target=new string('b',64);
    static int assertions;
    static void Check(bool ok,string reason){assertions++;if(!ok)throw new Exception(reason);}
    static void Reject(Action action,string reason){bool rejected=false;try{action();}catch(Exception error){if(error is InvalidDataException||error is ArgumentException||error is TargetInvocationException)rejected=true;else throw;}Check(rejected,reason);}
    static Dictionary<string,object> Device(string identity,bool current,string state="active"){return new Dictionary<string,object>{{"publicIdentity",identity},{"name",current?"Work PC":"Phone"},{"state",state},{"currentDevice",current},{"online",false},{"createdUtc",1700000000L},{"platform","unavailable"},{"version","unavailable"},{"cacheCapability","registered"}};}
    static Dictionary<string,object> Inventory(){return new Dictionary<string,object>{{"deviceManagementVersion",1},{"accountId",4L},{"serverEpoch",new string('c',32)},{"authorityRevision",2L},{"devices",new object[]{Device(Actor,true),Device(Target,false)}}};}
    static IEnumerable<T> Descendants<T>(DependencyObject root) where T:DependencyObject {if(root is T)yield return (T)root;for(int i=0;i<VisualTreeHelper.GetChildrenCount(root);i++)foreach(var value in Descendants<T>(VisualTreeHelper.GetChild(root,i)))yield return value;}
    static void Set(object target,string name,object value){target.GetType().GetField(name,BindingFlags.NonPublic|BindingFlags.Instance).SetValue(target,value);}
    [STAThread] public static int Main(string[] args){try{
        var snapshot=AccountDeviceSnapshot.Parse(Inventory(),4,Actor);
        Check(snapshot.Devices.Length==2&&snapshot.Devices[0].Current,"Inventory local identity");
        Check(snapshot.Devices[1].Platform=="Not available","Unknown platform not guessed");
        Check(snapshot.CanRevoke(snapshot.Devices[1])&&!snapshot.CanRevoke(snapshot.Devices[0]),"Self revoke policy");
        snapshot.Devices[1].State="revoked";Check(!snapshot.CanRevoke(snapshot.Devices[1]),"Already revoked");snapshot.Devices[1].State="active";
        var data=Inventory();data["accountId"]=3L;Reject(()=>AccountDeviceSnapshot.Parse(data,4,Actor),"Cross-account inventory");
        data=Inventory();((Dictionary<string,object>)((object[])data["devices"])[1])["currentDevice"]=true;Reject(()=>AccountDeviceSnapshot.Parse(data,4,Actor),"Forged current device");
        data=Inventory();data["devices"]=new object[]{Device(Actor,true),Device(Actor,true)};Reject(()=>AccountDeviceSnapshot.Parse(data,4,Actor),"Duplicate identity");
        data=Inventory();((Dictionary<string,object>)((object[])data["devices"])[1])["state"]="unknown";Reject(()=>AccountDeviceSnapshot.Parse(data,4,Actor),"Invalid authorization");
        data=Inventory();data["devices"]=new object[]{Device(Target,false)};Reject(()=>AccountDeviceSnapshot.Parse(data,4,Actor),"Absent actor");
        Check(AccountDeviceSnapshot.NormalizeName("  Cafe\u0301 \U0001F600  ")=="Café \U0001F600","Unicode normalized label");
        foreach(string invalid in new[]{"",new string('x',65),"hello\nworld","visible\u202Ehidden","\ud800"})Reject(()=>AccountDeviceSnapshot.NormalizeName(invalid),"Unsafe name accepted");
        var next=AccountDeviceSnapshot.Parse(Inventory(),4,Actor);next.Revision=1;Check(!AccountDeviceSnapshot.CanApply(snapshot,next),"Revision rollback");next.Revision=3;next.Epoch=new string('d',32);Check(AccountDeviceSnapshot.CanApply(snapshot,next),"Restart higher revision");next.Account=5;Check(!AccountDeviceSnapshot.CanApply(snapshot,next),"Profile callback isolation");
        next=AccountDeviceSnapshot.Parse(Inventory(),4,Actor);next.Devices[1].Online=true;next.Devices[1].Platform="Android";next.Devices[1].Version="fixture";Check(AccountDeviceSnapshot.CanApply(snapshot,next),"Presence/self metadata may change at same authority revision");next.Devices[1].Name="Changed";Check(!AccountDeviceSnapshot.CanApply(snapshot,next),"Same-revision rename rejected");next.Devices[1].Name="Phone";next.Devices[1].Identity=new string('d',64);Check(!AccountDeviceSnapshot.CanApply(snapshot,next),"Same-revision substitution rejected");
        next=AccountDeviceSnapshot.Parse(Inventory(),4,Actor);snapshot.Devices[1].State="revoked";next.Revision=3;next.Epoch=new string('d',32);Check(!AccountDeviceSnapshot.CanApply(snapshot,next),"Restart cannot reactivate tombstone");next.Devices=new[]{next.Devices[0]};Check(!AccountDeviceSnapshot.CanApply(snapshot,next),"Revoked record cannot disappear");snapshot.Devices[1].State="active";
        Check(DeviceManagementClient.Failure(new DiscoveryRequestException("revision_conflict")).Contains("Refresh"),"Conflict actionable");
        Check(DeviceManagementClient.Failure(new DiscoveryRequestException("step_up_failed")).Contains("Nothing was revoked"),"Failed password no success claim");
        long seconds=DateTimeOffset.UtcNow.ToUnixTimeSeconds();string pin=new string('d',64),binding=new string('e',64),nonce=new string('f',64),epoch=new string('c',32);
        string canonical="PeerChat device management v1|revoke|4|"+Actor+"|"+Target+"|2|"+pin+"|"+epoch+"|"+seconds+"|"+(seconds+60)+"|"+nonce+"|"+binding;
        var challenge=new Dictionary<string,object>{{"purpose","revoke"},{"accountId",4L},{"publicIdentity",Actor},{"targetPublicIdentity",Target},{"expectedRevision",2L},{"serverFingerprint",pin},{"serverEpoch",epoch},{"issuedUtc",seconds},{"expiresUtc",seconds+60},{"challengeId",nonce},{"sessionBinding",binding},{"signatureInput",Convert.ToBase64String(Encoding.ASCII.GetBytes(canonical))}};
        var proof=typeof(DeviceManagementClient).GetMethod("CreateProof",BindingFlags.Static|BindingFlags.NonPublic);int calls=0;Func<byte[],byte[]> signer=bytes=>{calls++;throw new InvalidDataException("fixture stops at native boundary");};
        foreach(string field in new[]{"purpose","targetPublicIdentity","serverFingerprint","sessionBinding","signatureInput"}){object original=challenge[field];challenge[field]="invalid";Reject(()=>proof.Invoke(null,new object[]{signer,challenge,4L,Actor,Target,2L,pin,binding,DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}),"Challenge "+field);Check(calls==0,"Invalid challenge reached signer");challenge[field]=original;}
        Reject(()=>proof.Invoke(null,new object[]{signer,challenge,4L,Actor,Target,2L,pin,binding,DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}),"Fixture native stop");Check(calls==1,"Correct challenge reaches scoped signer only");
        // Build the actual page with no application startup, native profile, or network.
        var app=new Application();var desktop=(DesktopApplication)FormatterServices.GetUninitializedObject(typeof(DesktopApplication));
        Set(desktop,"managedDeviceStatus","Isolated verified inventory");var discovery=new DiscoveryClient("localhost",7443,pin){AccountId=4L};Set(desktop,"discovery",discovery);Set(desktop,"managedDeviceOwner",discovery);Set(desktop,"managedDeviceGeneration",1UL);Set(desktop,"discoverySessionGeneration",1UL);Set(desktop,"managedDeviceBusy",true);Set(desktop,"managedDevices",snapshot);
        ResourceDictionary resources;using(var source=typeof(DesktopApplication).Assembly.GetManifestResourceStream("Main.xaml")){var existing=(Window)System.Windows.Markup.XamlReader.Load(source);resources=existing.Resources;}
        var window=new Window{Width=820,Height=800,Resources=resources,FontFamily=new FontFamily("Segoe UI"),FontSize=15,Background=new SolidColorBrush(Color.FromRgb(12,17,22)),Foreground=Brushes.White,ShowInTaskbar=false,ShowActivated=false,Left=-20000,WindowStartupLocation=WindowStartupLocation.Manual};
        var panel=(StackPanel)typeof(DesktopApplication).GetMethod("DeviceManagerPanel",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(desktop,new object[]{window});window.Content=new ScrollViewer{Content=panel,VerticalScrollBarVisibility=ScrollBarVisibility.Auto};window.Show();window.UpdateLayout();
        Check(Descendants<Button>(window).Count(b=>Object.Equals(b.Content,"Rename"))==2,"Device cards");
        Check(Descendants<Button>(window).Count(b=>Object.Equals(b.Content,"Revoke device"))==2,"Revoke action distinct from history grants");
        Check(Descendants<Button>(window).All(b=>!b.IsEnabled),"Busy state prevents duplicate mutation");
        Set(desktop,"managedDeviceBusy",false);typeof(DesktopApplication).GetMethod("PublishManagedDevices",BindingFlags.NonPublic|BindingFlags.Instance).Invoke(desktop,null);window.UpdateLayout();
        Check(Descendants<Button>(window).Count(b=>Object.Equals(b.Content,"Revoke device")&&b.IsEnabled)==1,"Current device cannot revoke self");
        foreach(var expand in Descendants<Expander>(window).ToArray())expand.IsExpanded=true;window.UpdateLayout();Check(Descendants<TextBox>(window).Count(t=>t.IsReadOnly&&t.Text.Length==64)==2,"Full selectable fingerprints");
        if(args.Length>0){Directory.CreateDirectory(args[0]);panel.Children.Insert(0,new TextBlock{Text="ISOLATED UI FIXTURE — SAMPLE DEVICES, NOT YOUR ACCOUNT",Foreground=Brushes.Orange,TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,0,0,12)});window.UpdateLayout();foreach(double scale in new[]{1.0,1.5,2.0}){var bitmap=new RenderTargetBitmap((int)(window.ActualWidth*scale),(int)(window.ActualHeight*scale),96*scale,96*scale,PixelFormats.Pbgra32);bitmap.Render(window);var png=new PngBitmapEncoder();png.Frames.Add(BitmapFrame.Create(bitmap));using(var output=File.Create(Path.Combine(args[0],"device-manager-"+(int)(scale*100)+".png")))png.Save(output);}}
        window.Width=460;window.Height=360;window.UpdateLayout();Check(window.ActualWidth==460&&Descendants<ScrollViewer>(window).Any(),"Small-window scroll");
        Set(desktop,"discoverySessionGeneration",2UL);typeof(DesktopApplication).GetMethod("PublishManagedDevices",BindingFlags.NonPublic|BindingFlags.Instance).Invoke(desktop,null);window.UpdateLayout();Check(!Descendants<Button>(window).Any(b=>Object.Equals(b.Content,"Revoke device")),"Replaced login hides stale mutation controls");
        window.Close();app.Shutdown();Console.WriteLine("PASS Device Manager model, challenge validation, WPF cards/details/scroll, generation fencing: "+assertions+" assertions. No live accounts or native keys touched.");return 0;
    }catch(Exception error){Console.Error.WriteLine(error);return 1;}}
}
