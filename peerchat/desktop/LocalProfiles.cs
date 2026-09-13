using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;

namespace PeerChatDesktop {
public sealed class LocalProfileInfo {
    public string Id {get;set;}
    public string Name {get;set;}
    public string CreatedUtc {get;set;}
    public string LastUsedUtc {get;set;}
    public bool MigratedLegacy {get;set;}
}
public sealed class LocalProfileCatalog {
    public int Version {get;set;}
    public string SelectedProfileId {get;set;}
    public List<LocalProfileInfo> Profiles {get;set;}
}

// One process exclusively owns one profile directory. Different profile directories use
// different lock files and remain intentionally runnable at the same time.
public sealed class LocalProfileLease : IDisposable {
    readonly FileStream stream;
    LocalProfileLease(FileStream value){stream=value;}
    public static LocalProfileLease TryAcquire(string profileDirectory){
        try{
            string directory=Path.GetFullPath(profileDirectory);Directory.CreateDirectory(directory);
            return new LocalProfileLease(new FileStream(Path.Combine(directory,".peerchat-profile.lock"),FileMode.OpenOrCreate,FileAccess.ReadWrite,FileShare.None));
        }catch(IOException){return null;}catch(UnauthorizedAccessException){return null;}
    }
    public void Dispose(){stream.Dispose();}
}

// Installation metadata lives above Profiles. Nothing beneath one stable profile ID is ever
// consulted while another profile is active. Usernames are deliberately not directory names.
public sealed class LocalProfileManager {
    const int CatalogVersion=1;
    readonly JavaScriptSerializer json=new JavaScriptSerializer();
    public string InstallRoot {get;private set;}
    public string ProfilesRoot {get{return Path.Combine(InstallRoot,"Profiles");}}
    public string CatalogPath {get{return Path.Combine(InstallRoot,"profiles.json");}}
    public LocalProfileManager(string installRoot){InstallRoot=Path.GetFullPath(installRoot);}

    public LocalProfileCatalog Initialize(){
        Directory.CreateDirectory(InstallRoot);Directory.CreateDirectory(ProfilesRoot);
        var catalog=LoadCatalog();
        if(catalog==null)catalog=RecoverMigratedCatalog();
        if(catalog==null&&HasLegacyState())catalog=MigrateLegacy();
        if(catalog==null){catalog=EmptyCatalog();var created=CreateProfile(catalog,"Local profile");catalog.SelectedProfileId=created.Id;SaveCatalog(catalog);}
        ValidateCatalog(catalog);return catalog;
    }
    public LocalProfileCatalog Load(){var catalog=LoadCatalog();if(catalog==null)throw new InvalidOperationException("Local profile catalog is missing or invalid.");ValidateCatalog(catalog);return catalog;}
    public string ProfilePath(string profileId){ValidateId(profileId);return Path.Combine(ProfilesRoot,profileId);}
    public LocalProfileInfo Create(string name){var catalog=Load();var profile=CreateProfile(catalog,name);catalog.SelectedProfileId=profile.Id;profile.LastUsedUtc=DateTime.UtcNow.ToString("O");SaveCatalog(catalog);return profile;}
    public void Select(string profileId){var catalog=Load();var profile=catalog.Profiles.FirstOrDefault(p=>p.Id==profileId);if(profile==null)throw new InvalidOperationException("The selected local profile does not exist.");catalog.SelectedProfileId=profileId;profile.LastUsedUtc=DateTime.UtcNow.ToString("O");SaveCatalog(catalog);}
    public void Rename(string profileId,string name){var catalog=Load();var profile=catalog.Profiles.FirstOrDefault(p=>p.Id==profileId);if(profile==null)throw new InvalidOperationException("The local profile does not exist.");profile.Name=CleanName(name);SaveCatalog(catalog);}
    public void Remove(string profileId){var catalog=Load();if(catalog.Profiles.Count<=1)throw new InvalidOperationException("Create another profile before removing the only local profile.");var profile=catalog.Profiles.FirstOrDefault(p=>p.Id==profileId);if(profile==null)throw new InvalidOperationException("The local profile does not exist.");string source=ProfilePath(profileId),removedRoot=Path.Combine(InstallRoot,"RemovedProfiles");Directory.CreateDirectory(removedRoot);Directory.Move(source,Path.Combine(removedRoot,profileId+"-"+DateTime.UtcNow.ToString("yyyyMMdd-HHmmss")));catalog.Profiles.Remove(profile);if(catalog.SelectedProfileId==profileId)catalog.SelectedProfileId=catalog.Profiles[0].Id;SaveCatalog(catalog);}
    public LocalProfileInfo Selected(){var catalog=Load();return catalog.Profiles.First(p=>p.Id==catalog.SelectedProfileId);}

    public string BackupSelected(string destinationRoot){var selected=Selected();string destination=Path.Combine(destinationRoot,"PeerChatProfile");CopyAndVerify(ProfilePath(selected.Id),destination);File.WriteAllText(Path.Combine(destinationRoot,"profile-metadata.json"),json.Serialize(selected),new UTF8Encoding(false));return destination;}
    public void ResetSelected(string backupRoot){var selected=Selected();string live=ProfilePath(selected.Id);Directory.CreateDirectory(backupRoot);if(Directory.Exists(live))CopyAndVerify(live,Path.Combine(backupRoot,"PeerChatProfile"));Directory.Move(live,Path.Combine(backupRoot,"PeerChatProfile-reset-source"));Directory.CreateDirectory(live);}
    public void RestoreSelected(string savedProfile,string safetyRoot){
        var selected=Selected();string live=ProfilePath(selected.Id),staging=Path.Combine(ProfilesRoot,".restore-"+Guid.NewGuid().ToString("N"));
        CopyAndVerify(savedProfile,staging);Directory.CreateDirectory(safetyRoot);
        string safety=Path.Combine(safetyRoot,"PeerChatProfile");if(Directory.Exists(live))Directory.Move(live,safety);
        try{Directory.Move(staging,live);}catch{if(!Directory.Exists(live)&&Directory.Exists(safety))Directory.Move(safety,live);throw;}
    }

    LocalProfileCatalog EmptyCatalog(){return new LocalProfileCatalog{Version=CatalogVersion,Profiles=new List<LocalProfileInfo>()};}
    LocalProfileInfo CreateProfile(LocalProfileCatalog catalog,string name){
        string id=Guid.NewGuid().ToString("N");var profile=new LocalProfileInfo{Id=id,Name=CleanName(name),CreatedUtc=DateTime.UtcNow.ToString("O"),LastUsedUtc=DateTime.UtcNow.ToString("O")};
        Directory.CreateDirectory(ProfilePath(id));catalog.Profiles.Add(profile);return profile;
    }
    static string CleanName(string name){name=(name??"").Trim();if(name.Length==0)return "Local profile";return name.Length<=64?name:name.Substring(0,64);}
    bool HasLegacyState(){return File.Exists(Path.Combine(InstallRoot,"peerchat.sqlite"))||File.Exists(Path.Combine(InstallRoot,"discovery-session.dat"))||File.Exists(Path.Combine(InstallRoot,"discovery-service.dat"));}
    LocalProfileCatalog MigrateLegacy(){
        string id=Guid.NewGuid().ToString("N"),stamp=DateTime.UtcNow.ToString("yyyyMMdd-HHmmss"),backup=Path.Combine(InstallRoot,"LegacyBackups",stamp+"-"+id.Substring(0,8),"PeerChatLegacy"),staging=Path.Combine(ProfilesRoot,".migrate-"+id),final=ProfilePath(id);
        Directory.CreateDirectory(backup);Directory.CreateDirectory(staging);
        foreach(string file in Directory.GetFiles(InstallRoot)){string name=Path.GetFileName(file);if(String.Equals(name,"profiles.json",StringComparison.OrdinalIgnoreCase)||name.StartsWith("profiles.json.",StringComparison.OrdinalIgnoreCase)||name.StartsWith("profile-migration",StringComparison.OrdinalIgnoreCase))continue;File.Copy(file,Path.Combine(backup,name),true);File.Copy(file,Path.Combine(staging,name),true);}
        foreach(string child in Directory.GetDirectories(InstallRoot)){string name=Path.GetFileName(child);if(String.Equals(name,"Profiles",StringComparison.OrdinalIgnoreCase)||String.Equals(name,"LegacyBackups",StringComparison.OrdinalIgnoreCase)||String.Equals(name,"RemovedProfiles",StringComparison.OrdinalIgnoreCase))continue;CopyDirectory(child,Path.Combine(backup,name));CopyDirectory(child,Path.Combine(staging,name));}
        VerifySame(backup,staging);var profile=new LocalProfileInfo{Id=id,Name="Migrated local profile",CreatedUtc=DateTime.UtcNow.ToString("O"),LastUsedUtc=DateTime.UtcNow.ToString("O"),MigratedLegacy=true};
        File.WriteAllText(Path.Combine(staging,"profile-migration-complete.json"),json.Serialize(profile),new UTF8Encoding(false));Directory.Move(staging,final);
        var catalog=EmptyCatalog();catalog.Profiles.Add(profile);catalog.SelectedProfileId=id;SaveCatalog(catalog);AppendSafeMigrationLog("Legacy profile copied, verified, and activated. Original files retained; backup created.");return catalog;
    }
    LocalProfileCatalog RecoverMigratedCatalog(){
        foreach(string directory in Directory.GetDirectories(ProfilesRoot)){string marker=Path.Combine(directory,"profile-migration-complete.json");if(!File.Exists(marker))continue;try{var profile=json.Deserialize<LocalProfileInfo>(File.ReadAllText(marker));if(profile!=null&&profile.Id==Path.GetFileName(directory)){var catalog=EmptyCatalog();catalog.Profiles.Add(profile);catalog.SelectedProfileId=profile.Id;SaveCatalog(catalog);AppendSafeMigrationLog("Recovered the verified migrated profile catalog after an interrupted activation.");return catalog;}}catch{}}
        return null;
    }
    void AppendSafeMigrationLog(string message){try{File.AppendAllText(Path.Combine(InstallRoot,"profile-migration.log"),DateTime.UtcNow.ToString("O")+" "+message+Environment.NewLine,new UTF8Encoding(false));}catch{}}
    LocalProfileCatalog LoadCatalog(){try{if(!File.Exists(CatalogPath))return null;return json.Deserialize<LocalProfileCatalog>(File.ReadAllText(CatalogPath));}catch{return null;}}
    void SaveCatalog(LocalProfileCatalog catalog){
        ValidateCatalog(catalog);string temporary=CatalogPath+".new";File.WriteAllText(temporary,json.Serialize(catalog),new UTF8Encoding(false));
        if(File.Exists(CatalogPath)){string previous=CatalogPath+".previous";File.Replace(temporary,CatalogPath,previous,true);}else File.Move(temporary,CatalogPath);
    }
    void ValidateCatalog(LocalProfileCatalog catalog){if(catalog==null||catalog.Version!=CatalogVersion||catalog.Profiles==null||catalog.Profiles.Count==0)throw new InvalidOperationException("Local profile catalog is invalid.");var ids=new HashSet<string>(StringComparer.Ordinal);foreach(var profile in catalog.Profiles){ValidateId(profile.Id);if(!ids.Add(profile.Id))throw new InvalidOperationException("Duplicate local profile ID.");if(!Directory.Exists(ProfilePath(profile.Id)))throw new InvalidOperationException("A local profile directory is missing.");}if(!ids.Contains(catalog.SelectedProfileId))throw new InvalidOperationException("Selected local profile is missing.");}
    static void ValidateId(string id){Guid parsed;if(String.IsNullOrEmpty(id)||id.Length!=32||!Guid.TryParseExact(id,"N",out parsed))throw new InvalidOperationException("Invalid local profile ID.");}
    static void CopyAndVerify(string source,string target){if(!Directory.Exists(source))throw new DirectoryNotFoundException("Profile source was not found.");CopyDirectory(source,target);VerifySame(source,target);}
    static void CopyDirectory(string source,string target){Directory.CreateDirectory(target);foreach(string file in Directory.GetFiles(source))File.Copy(file,Path.Combine(target,Path.GetFileName(file)),true);foreach(string child in Directory.GetDirectories(source))CopyDirectory(child,Path.Combine(target,Path.GetFileName(child)));}
    static void VerifySame(string source,string target){var left=Directory.GetFiles(source,"*",SearchOption.AllDirectories).Select(path=>Relative(source,path)).OrderBy(path=>path,StringComparer.Ordinal).ToArray();var right=Directory.GetFiles(target,"*",SearchOption.AllDirectories).Select(path=>Relative(target,path)).OrderBy(path=>path,StringComparer.Ordinal).ToArray();if(!left.SequenceEqual(right,StringComparer.Ordinal))throw new IOException("Profile copy verification failed.");using(var hash=SHA256.Create())for(int i=0;i<left.Length;i++){string a=Path.Combine(source,left[i]),b=Path.Combine(target,right[i]);if(new FileInfo(a).Length!=new FileInfo(b).Length)throw new IOException("Profile copy verification failed.");byte[] ah,bh;using(var stream=File.OpenRead(a))ah=hash.ComputeHash(stream);using(var stream=File.OpenRead(b))bh=hash.ComputeHash(stream);if(!ah.SequenceEqual(bh))throw new IOException("Profile copy verification failed.");}}
    static string Relative(string root,string path){return path.Substring(root.TrimEnd(Path.DirectorySeparatorChar).Length+1);}
}
}
