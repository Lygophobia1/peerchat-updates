using System;
using System.IO;
using System.Linq;
using PeerChatDesktop;

class ProfileManagerTest {
    static void Require(bool condition,string message){if(!condition)throw new Exception(message);}
    static int Main(){try{
        string root=Path.Combine(Environment.CurrentDirectory,"build","tests","runtime","profiles-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(root);
        byte[] encryptedHistory={1,7,3,9,11,42};File.WriteAllBytes(Path.Combine(root,"peerchat.sqlite"),encryptedHistory);File.WriteAllBytes(Path.Combine(root,"discovery-session.dat"),new byte[]{8,6,4,2});
        var manager=new LocalProfileManager(root);var first=manager.Initialize();var a=first.Profiles.Single();Require(a.MigratedLegacy,"Legacy profile was not migrated");Require(File.ReadAllBytes(Path.Combine(manager.ProfilePath(a.Id),"peerchat.sqlite")).SequenceEqual(encryptedHistory),"Legacy encrypted history bytes changed");Require(File.Exists(Path.Combine(root,"peerchat.sqlite")),"Legacy source was not retained");
        var secondPass=manager.Initialize();Require(secondPass.Profiles.Count==1&&secondPass.Profiles[0].Id==a.Id,"Migration was not idempotent");File.WriteAllText(Path.Combine(manager.ProfilePath(a.Id),"a-history.marker"),"A only");File.WriteAllText(Path.Combine(manager.ProfilePath(a.Id),"a-trust.marker"),"A only");
        var b=manager.Create("Profile B");Require(a.Id!=b.Id,"Stable profiles reused an ID");Require(!File.Exists(Path.Combine(manager.ProfilePath(b.Id),"a-history.marker"))&&!File.Exists(Path.Combine(manager.ProfilePath(b.Id),"a-trust.marker")),"A state appeared in B");
        manager.Select(a.Id);Require(manager.Selected().Id==a.Id,"Selector did not choose A");manager.Select(b.Id);Require(manager.Selected().Id==b.Id,"Selector did not choose B");manager.Select(a.Id);Require(File.Exists(Path.Combine(manager.ProfilePath(a.Id),"a-history.marker")),"A-B-A switch did not restore A path");
        manager.Select(b.Id);File.WriteAllText(Path.Combine(manager.ProfilePath(b.Id),"identity.marker"),"B identity");string backup=Path.Combine(root,"backup");manager.BackupSelected(backup);manager.ResetSelected(Path.Combine(root,"reset"));Require(!File.Exists(Path.Combine(manager.ProfilePath(b.Id),"identity.marker")),"Reset B retained B state");Require(File.Exists(Path.Combine(manager.ProfilePath(a.Id),"a-history.marker")),"Reset B changed A");manager.RestoreSelected(Path.Combine(backup,"PeerChatProfile"),Path.Combine(root,"safety"));Require(File.ReadAllText(Path.Combine(manager.ProfilePath(b.Id),"identity.marker"))=="B identity","Restore B failed");Require(File.Exists(Path.Combine(manager.ProfilePath(a.Id),"a-trust.marker")),"Restore B changed A");
        manager.Rename(b.Id,"Renamed B");Require(manager.Selected().Name=="Renamed B","Rename did not update the selected card");manager.Remove(a.Id);Require(manager.Load().Profiles.Count==1&&Directory.Exists(Path.Combine(root,"RemovedProfiles")),"Confirmed local-profile removal was not isolated/recoverable");
        Console.WriteLine("PASS: profile catalog, idempotent verified legacy copy, selector path, A/B isolation, switch, reset, restore, rename, and recoverable remove.");return 0;
    }catch(Exception error){Console.Error.WriteLine("FAIL: "+error);return 1;}}
}
