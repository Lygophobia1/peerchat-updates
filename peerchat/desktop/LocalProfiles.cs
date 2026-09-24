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
public enum ProfileMaintenanceAction { Backup, Reset, Restore }
// Maintenance requests deliberately live above Profiles and name only a catalog profile ID.
// They never contain a caller-controlled live path or a request to delete arbitrary data.
public sealed class ProfileMaintenanceRequest {
    public int Version {get;set;}
    public string RequestId {get;set;}
    public string ProfileId {get;set;}
    public string Action {get;set;}
    public string BackupRoot {get;set;}
    public string RestoreRoot {get;set;}
    public string CreatedUtc {get;set;}
}
public sealed class ProfileBackupMetadata {
    public int Version {get;set;}
    public string ProfileId {get;set;}
    public string ProfileName {get;set;}
    public string CreatedUtc {get;set;}
    public string PeerId {get;set;}
    public long AccountId {get;set;}
    public string Username {get;set;}
    public string ApplicationVersion {get;set;}
    public string StorageFormat {get;set;}
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
    public string MaintenanceRoot {get{return Path.Combine(InstallRoot,"Maintenance");}}
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

    public ProfileMaintenanceRequest CreateMaintenanceRequest(ProfileMaintenanceAction action,string profileId,string backupRoot,string restoreRoot=null){
        var catalog=Load();if(catalog.Profiles.All(p=>p.Id!=profileId))throw new InvalidOperationException("The selected local profile does not exist.");
        if(String.IsNullOrWhiteSpace(backupRoot))throw new InvalidOperationException("A maintenance recovery location is required.");
        Directory.CreateDirectory(MaintenanceRoot);var request=new ProfileMaintenanceRequest{Version=1,RequestId=Guid.NewGuid().ToString("N"),ProfileId=profileId,Action=action.ToString(),BackupRoot=Path.GetFullPath(backupRoot),RestoreRoot=String.IsNullOrWhiteSpace(restoreRoot)?null:Path.GetFullPath(restoreRoot),CreatedUtc=DateTime.UtcNow.ToString("O")};
        File.WriteAllText(RequestPath(request.RequestId),json.Serialize(request),new UTF8Encoding(false));return request;
    }
    public ProfileMaintenanceRequest ReadMaintenanceRequest(string requestId){
        if(String.IsNullOrEmpty(requestId)||requestId.Length!=32||!requestId.All(c=>Uri.IsHexDigit(c)))throw new InvalidOperationException("Invalid profile maintenance request.");
        ProfileMaintenanceRequest request;try{request=json.Deserialize<ProfileMaintenanceRequest>(File.ReadAllText(RequestPath(requestId)));}catch(Exception error){throw new InvalidOperationException("The profile maintenance request could not be read.",error);}
        ProfileMaintenanceAction action;if(request==null||request.Version!=1||request.RequestId!=requestId||!Enum.TryParse(request.Action,out action)||String.IsNullOrWhiteSpace(request.BackupRoot))throw new InvalidOperationException("The profile maintenance request is invalid.");
        ValidateId(request.ProfileId);var catalog=Load();if(catalog.Profiles.All(p=>p.Id!=request.ProfileId))throw new InvalidOperationException("The requested local profile no longer exists.");return request;
    }
    public void CompleteMaintenanceRequest(ProfileMaintenanceRequest request){if(request!=null&&File.Exists(RequestPath(request.RequestId)))File.Delete(RequestPath(request.RequestId));}
    public void ExecuteMaintenance(ProfileMaintenanceRequest request,Func<string,string,string> rebuildIdentity){
        if(request==null)throw new ArgumentNullException("request");ProfileMaintenanceAction action;if(!Enum.TryParse(request.Action,out action))throw new InvalidOperationException("Invalid profile maintenance action.");
        string live=ProfilePath(request.ProfileId);EnsureProfileIsClosed(live,action);var profile=Load().Profiles.First(p=>p.Id==request.ProfileId);
        if(action==ProfileMaintenanceAction.Backup){CreateBackup(live,request.BackupRoot,profile,rebuildIdentity==null?"":rebuildIdentity(live,null));return;}
        if(action==ProfileMaintenanceAction.Reset){ResetClosedProfile(live,request.BackupRoot,profile,rebuildIdentity);return;}
        if(action==ProfileMaintenanceAction.Restore){RestoreClosedProfile(live,request.BackupRoot,request.RestoreRoot,profile);return;}
        throw new InvalidOperationException("Unsupported profile maintenance action.");
    }

    void EnsureProfileIsClosed(string live,ProfileMaintenanceAction action){using(var lease=LocalProfileLease.TryAcquire(live)){if(lease==null)throw new InvalidOperationException("This profile is currently open in another PeerChat process. Close it before "+action.ToString().ToLowerInvariant()+"ing.");}}
    void CreateBackup(string live,string backupRoot,LocalProfileInfo profile,string peerId){
        if(Directory.Exists(backupRoot))throw new IOException("The selected backup folder already exists.");Directory.CreateDirectory(backupRoot);string destination=Path.Combine(backupRoot,"PeerChatProfile");
        try{CopyAndVerify(live,destination);WriteBackupMetadata(backupRoot,profile,peerId??"",live);}catch{try{if(Directory.Exists(backupRoot))Directory.Delete(backupRoot,true);}catch{}throw;}
    }
    void ResetClosedProfile(string live,string backupRoot,LocalProfileInfo profile,Func<string,string,string> rebuildIdentity){
        if(rebuildIdentity==null)throw new InvalidOperationException("The local identity rebuild service is unavailable.");if(Directory.Exists(backupRoot))throw new IOException("The selected reset recovery folder already exists.");Directory.CreateDirectory(backupRoot);
        string recovery=Path.Combine(backupRoot,"PeerChatProfile-reset-recovery"),staging=Path.Combine(ProfilesRoot,".reset-"+profile.Id+"-"+Guid.NewGuid().ToString("N"));
        try{
            Directory.Move(live,recovery);Directory.CreateDirectory(staging);string peerId=rebuildIdentity(recovery,staging)??"";
            string oldLink=Path.Combine(recovery,"account-link.dat");if(File.Exists(oldLink)){string newLink=Path.Combine(staging,"account-link.dat");File.Copy(oldLink,newLink,false);if(new ProfileAccountStore(staging).ClearSession()==null)File.Delete(newLink);}
            WriteBackupMetadata(backupRoot,profile,peerId,recovery);Directory.Move(staging,live);
        }catch{try{if(Directory.Exists(staging))Directory.Delete(staging,true);if(!Directory.Exists(live)&&Directory.Exists(recovery))Directory.Move(recovery,live);}catch{}throw;}
    }
    void RestoreClosedProfile(string live,string safetyRoot,string restoreRoot,LocalProfileInfo profile){
        if(String.IsNullOrWhiteSpace(restoreRoot))throw new InvalidOperationException("A profile backup is required for restore.");string source=Path.Combine(restoreRoot,"PeerChatProfile"),metadataPath=Path.Combine(restoreRoot,"profile-metadata.json");
        if(!Directory.Exists(source)||!File.Exists(metadataPath))throw new InvalidOperationException("That folder is not a complete PeerChat profile backup.");ProfileBackupMetadata metadata;try{metadata=json.Deserialize<ProfileBackupMetadata>(File.ReadAllText(metadataPath));}catch(Exception error){throw new InvalidOperationException("The selected profile backup metadata is invalid.",error);}if(metadata==null||metadata.Version!=1||metadata.ProfileId!=profile.Id)throw new InvalidOperationException("That backup belongs to a different local profile.");
        if(Directory.Exists(safetyRoot))throw new IOException("The selected restore recovery folder already exists.");Directory.CreateDirectory(safetyRoot);string staging=Path.Combine(ProfilesRoot,".restore-"+profile.Id+"-"+Guid.NewGuid().ToString("N")),safety=Path.Combine(safetyRoot,"PeerChatProfile-pre-restore");
        try{CopyAndVerify(source,staging);Directory.Move(live,safety);Directory.Move(staging,live);WriteBackupMetadata(safetyRoot,profile,metadata.PeerId,safety);}catch{try{if(Directory.Exists(staging))Directory.Delete(staging,true);if(!Directory.Exists(live)&&Directory.Exists(safety))Directory.Move(safety,live);}catch{}throw;}
    }
    void WriteBackupMetadata(string root,LocalProfileInfo profile,string peerId,string profileDirectory){
        var account=new ProfileAccountStore(profileDirectory).Load();var metadata=new ProfileBackupMetadata{Version=1,ProfileId=profile.Id,ProfileName=profile.Name,CreatedUtc=DateTime.UtcNow.ToString("O"),PeerId=peerId??"",AccountId=account==null?0:account.AccountId,Username=account==null?"":account.Username,ApplicationVersion=PeerChat.VersionInfo.Display,StorageFormat="current encrypted SQLite profile"};
        File.WriteAllText(Path.Combine(root,"profile-metadata.json"),json.Serialize(metadata),new UTF8Encoding(false));
    }
    string RequestPath(string requestId){return Path.Combine(MaintenanceRoot,"profile-maintenance-"+requestId+".json");}

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
    static bool RuntimeOnlyFile(string path){string name=Path.GetFileName(path);return String.Equals(name,".peerchat-profile.lock",StringComparison.OrdinalIgnoreCase)||String.Equals(name,"peerchat.sqlite-wal",StringComparison.OrdinalIgnoreCase)||String.Equals(name,"peerchat.sqlite-shm",StringComparison.OrdinalIgnoreCase)||name.EndsWith(".tmp",StringComparison.OrdinalIgnoreCase);}
    static void CopyAndVerify(string source,string target){if(!Directory.Exists(source))throw new DirectoryNotFoundException("Profile source was not found.");CopyDirectory(source,target);VerifySame(source,target);}
    static void CopyDirectory(string source,string target){Directory.CreateDirectory(target);foreach(string file in Directory.GetFiles(source)){if(!RuntimeOnlyFile(file))File.Copy(file,Path.Combine(target,Path.GetFileName(file)),true);}foreach(string child in Directory.GetDirectories(source))CopyDirectory(child,Path.Combine(target,Path.GetFileName(child)));}
    static void VerifySame(string source,string target){var left=Directory.GetFiles(source,"*",SearchOption.AllDirectories).Where(path=>!RuntimeOnlyFile(path)).Select(path=>Relative(source,path)).OrderBy(path=>path,StringComparer.Ordinal).ToArray();var right=Directory.GetFiles(target,"*",SearchOption.AllDirectories).Where(path=>!RuntimeOnlyFile(path)).Select(path=>Relative(target,path)).OrderBy(path=>path,StringComparer.Ordinal).ToArray();if(!left.SequenceEqual(right,StringComparer.Ordinal))throw new IOException("Profile copy verification failed.");using(var hash=SHA256.Create())for(int i=0;i<left.Length;i++){string a=Path.Combine(source,left[i]),b=Path.Combine(target,right[i]);if(new FileInfo(a).Length!=new FileInfo(b).Length)throw new IOException("Profile copy verification failed.");byte[] ah,bh;using(var stream=File.OpenRead(a))ah=hash.ComputeHash(stream);using(var stream=File.OpenRead(b))bh=hash.ComputeHash(stream);if(!ah.SequenceEqual(bh))throw new IOException("Profile copy verification failed.");}}
    static string Relative(string root,string path){return path.Substring(root.TrimEnd(Path.DirectorySeparatorChar).Length+1);}
}
}
