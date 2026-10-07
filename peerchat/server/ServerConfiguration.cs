// One persisted configuration contract shared by the headless server and its local Admin UI.
// Mutable data never depends on the executable directory or the process current directory.
using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;

namespace PeerChat.ServerShared {
public sealed class ServerConfiguration {
 // PublicHostname identifies the discovery service. SelfHostedPublicP2PHost is an optional
 // address for a P2P listener on this host; its port always comes from client presence.
 public string DataDirectory,ListenAddress="0.0.0.0",PublicHostname="localhost",SelfHostedPublicP2PHost="",DatabasePath,LogDirectory,CertificateThumbprint="",CertificatePath="",CertificatePassword="",ManagementPipe="";
 // TURN credentials are issued by discovery, but the REST shared secret is local server
 // configuration. It is DPAPI-protected for the Windows user running this server and is never
 // serialized in plaintext or returned by the discovery protocol.
 public bool TurnEnabled=false;
 // FCM service credentials stay in an external administrator-managed file,
 // never in discovery responses, client packages or the server database.
 public bool FcmEnabled=false;
 public string FcmProjectId="",FcmServiceAccountPath="";
 public string TurnPublicHost="";
 public int TurnUdpPort=3478;
 public int TurnCredentialTtlSeconds=600;
 public string TurnRealm="peerchat";
 public string TurnRestSharedSecretProtected="";
 public string TurnRestSharedSecretFile="",CertificatePasswordFile="";
 [ScriptIgnore] public string TurnRestSharedSecret { get; private set; }
 public int Port=7443,SessionHours=336;
 public static string DefaultDataDirectory { get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),"PeerChat","Server"); } }
 public static string DefaultConfigPath { get { return Path.Combine(DefaultDataDirectory,"Config","server-config.json"); } }
 public static ServerConfiguration Load(string path){if(!File.Exists(path))return new ServerConfiguration{DataDirectory=DefaultDataDirectory};var value=new JavaScriptSerializer().Deserialize<ServerConfiguration>(File.ReadAllText(path));value.Normalize();return value;}
 public static string ProtectTurnRestSharedSecret(string value){if(String.IsNullOrWhiteSpace(value))return "";
#if PEERCHAT_PORTABLE
 return Convert.ToBase64String(PeerChat.Server.PortablePlatform.Dpapi(Encoding.UTF8.GetBytes(value),true));
#else
 return Convert.ToBase64String(ProtectedData.Protect(Encoding.UTF8.GetBytes(value),null,DataProtectionScope.CurrentUser));
#endif
 }
 public void SetTurnRestSharedSecret(string value){TurnRestSharedSecret=String.IsNullOrWhiteSpace(value)?"":value;TurnRestSharedSecretProtected=ProtectTurnRestSharedSecret(TurnRestSharedSecret);}
 void LoadTurnRestSharedSecret(){
#if PEERCHAT_PORTABLE
 if(TurnEnabled&&!String.IsNullOrEmpty(TurnRestSharedSecretFile)){if(!String.IsNullOrEmpty(TurnRestSharedSecretProtected))throw new InvalidOperationException("Configure only one TURN secret source");TurnRestSharedSecret=PeerChat.Server.PortablePlatform.ReadSecret(TurnRestSharedSecretFile,4096).TrimEnd('\r','\n');return;}
#endif
 if(!TurnEnabled||String.IsNullOrWhiteSpace(TurnRestSharedSecretProtected)){TurnRestSharedSecret="";return;}try{
#if PEERCHAT_PORTABLE
 TurnRestSharedSecret=Encoding.UTF8.GetString(PeerChat.Server.PortablePlatform.Dpapi(Convert.FromBase64String(TurnRestSharedSecretProtected),false));
#else
 TurnRestSharedSecret=Encoding.UTF8.GetString(ProtectedData.Unprotect(Convert.FromBase64String(TurnRestSharedSecretProtected),null,DataProtectionScope.CurrentUser));
#endif
}catch(Exception error){throw new InvalidOperationException("TURN is enabled but its protected REST shared secret cannot be opened for this Windows server user.",error);}}
 public void Normalize(){
#if PEERCHAT_PORTABLE
 if(!OperatingSystem.IsWindows()&&(String.IsNullOrWhiteSpace(DataDirectory)||!Path.IsPathRooted(DataDirectory)))throw new InvalidOperationException("Linux requires an explicit absolute data directory.");
#endif
if(String.IsNullOrWhiteSpace(DataDirectory))DataDirectory=DefaultDataDirectory;if(String.IsNullOrWhiteSpace(DatabasePath))DatabasePath=Path.Combine(DataDirectory,"Data","peerchat-server.db");if(String.IsNullOrWhiteSpace(LogDirectory))LogDirectory=Path.Combine(DataDirectory,"Logs");if(String.IsNullOrWhiteSpace(SelfHostedPublicP2PHost)&&!String.Equals(PublicHostname,"localhost",StringComparison.OrdinalIgnoreCase))SelfHostedPublicP2PHost=PublicHostname;LoadTurnRestSharedSecret();}
 public void Validate(){
#if PEERCHAT_PORTABLE
 if(!OperatingSystem.IsWindows()){
   if(String.IsNullOrEmpty(DataDirectory)||!Path.IsPathRooted(DataDirectory))throw new InvalidOperationException("Linux requires an explicit absolute data directory.");
   foreach(var path in new[]{DatabasePath,LogDirectory,CertificatePath})if(String.IsNullOrEmpty(path)||!Path.IsPathRooted(path))throw new InvalidOperationException("Linux requires absolute database, log and PFX paths.");
   if(!String.IsNullOrEmpty(CertificateThumbprint)||!String.IsNullOrEmpty(ManagementPipe))throw new InvalidOperationException("Windows certificate store/admin pipe are unavailable in the Linux daemon.");
   if(!String.IsNullOrEmpty(CertificatePassword)&&!String.IsNullOrEmpty(CertificatePasswordFile))throw new InvalidOperationException("Configure only one certificate password source.");
 }
#endif
Normalize();if(Port<1||Port>65535)throw new InvalidOperationException("TLS port must be 1 through 65535.");if(String.IsNullOrWhiteSpace(CertificateThumbprint)&&String.IsNullOrWhiteSpace(CertificatePath))throw new InvalidOperationException("A TLS certificate must be configured.");if(!TurnEnabled)return;if(String.IsNullOrWhiteSpace(TurnPublicHost)||TurnPublicHost.Length>253||TurnPublicHost.IndexOfAny(new[]{' ', '\t', '\r', '\n', '/', '@'})>=0)throw new InvalidOperationException("TURN public host is invalid.");if(TurnUdpPort<1||TurnUdpPort>65535)throw new InvalidOperationException("TURN UDP port must be 1 through 65535.");if(TurnCredentialTtlSeconds<120||TurnCredentialTtlSeconds>3600)throw new InvalidOperationException("TURN credential TTL must be between 120 and 3600 seconds.");if(String.IsNullOrWhiteSpace(TurnRealm)||TurnRealm.Length>128||TurnRealm.Any(c=>Char.IsControl(c)))throw new InvalidOperationException("TURN realm is invalid.");if(String.IsNullOrWhiteSpace(TurnRestSharedSecret)||TurnRestSharedSecret.Length<16)throw new InvalidOperationException("TURN is enabled but its REST shared secret is missing or too short.");}
 public void Save(string path){Normalize();Directory.CreateDirectory(Path.GetDirectoryName(path));Directory.CreateDirectory(Path.GetDirectoryName(DatabasePath));Directory.CreateDirectory(LogDirectory);File.WriteAllText(path,new JavaScriptSerializer().Serialize(this));}
}
}
