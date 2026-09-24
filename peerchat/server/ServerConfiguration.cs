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
 public string TurnPublicHost="";
 public int TurnUdpPort=3478;
 public int TurnCredentialTtlSeconds=600;
 public string TurnRealm="peerchat";
 public string TurnRestSharedSecretProtected="";
 [ScriptIgnore] public string TurnRestSharedSecret { get; private set; }
 public int Port=7443,SessionHours=336;
 public static string DefaultDataDirectory { get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),"PeerChat","Server"); } }
 public static string DefaultConfigPath { get { return Path.Combine(DefaultDataDirectory,"Config","server-config.json"); } }
 public static ServerConfiguration Load(string path){if(!File.Exists(path))return new ServerConfiguration{DataDirectory=DefaultDataDirectory};var value=new JavaScriptSerializer().Deserialize<ServerConfiguration>(File.ReadAllText(path));value.Normalize();return value;}
 public static string ProtectTurnRestSharedSecret(string value){if(String.IsNullOrWhiteSpace(value))return "";return Convert.ToBase64String(ProtectedData.Protect(Encoding.UTF8.GetBytes(value),null,DataProtectionScope.CurrentUser));}
 public void SetTurnRestSharedSecret(string value){TurnRestSharedSecret=String.IsNullOrWhiteSpace(value)?"":value;TurnRestSharedSecretProtected=ProtectTurnRestSharedSecret(TurnRestSharedSecret);}
 void LoadTurnRestSharedSecret(){if(!TurnEnabled||String.IsNullOrWhiteSpace(TurnRestSharedSecretProtected)){TurnRestSharedSecret="";return;}try{TurnRestSharedSecret=Encoding.UTF8.GetString(ProtectedData.Unprotect(Convert.FromBase64String(TurnRestSharedSecretProtected),null,DataProtectionScope.CurrentUser));}catch(Exception error){throw new InvalidOperationException("TURN is enabled but its protected REST shared secret cannot be opened for this Windows server user.",error);}}
 public void Normalize(){if(String.IsNullOrWhiteSpace(DataDirectory))DataDirectory=DefaultDataDirectory;if(String.IsNullOrWhiteSpace(DatabasePath))DatabasePath=Path.Combine(DataDirectory,"Data","peerchat-server.db");if(String.IsNullOrWhiteSpace(LogDirectory))LogDirectory=Path.Combine(DataDirectory,"Logs");if(String.IsNullOrWhiteSpace(SelfHostedPublicP2PHost)&&!String.Equals(PublicHostname,"localhost",StringComparison.OrdinalIgnoreCase))SelfHostedPublicP2PHost=PublicHostname;LoadTurnRestSharedSecret();}
 public void Validate(){Normalize();if(Port<1||Port>65535)throw new InvalidOperationException("TLS port must be 1 through 65535.");if(String.IsNullOrWhiteSpace(CertificateThumbprint))throw new InvalidOperationException("A TLS certificate must be configured.");if(!TurnEnabled)return;if(String.IsNullOrWhiteSpace(TurnPublicHost)||TurnPublicHost.Length>253||TurnPublicHost.IndexOfAny(new[]{' ', '\t', '\r', '\n', '/', '@'})>=0)throw new InvalidOperationException("TURN public host is invalid.");if(TurnUdpPort<1||TurnUdpPort>65535)throw new InvalidOperationException("TURN UDP port must be 1 through 65535.");if(TurnCredentialTtlSeconds<120||TurnCredentialTtlSeconds>3600)throw new InvalidOperationException("TURN credential TTL must be between 120 and 3600 seconds.");if(String.IsNullOrWhiteSpace(TurnRealm)||TurnRealm.Length>128||TurnRealm.Any(c=>Char.IsControl(c)))throw new InvalidOperationException("TURN realm is invalid.");if(String.IsNullOrWhiteSpace(TurnRestSharedSecret)||TurnRestSharedSecret.Length<16)throw new InvalidOperationException("TURN is enabled but its REST shared secret is missing or too short.");}
 public void Save(string path){Normalize();Directory.CreateDirectory(Path.GetDirectoryName(path));Directory.CreateDirectory(Path.GetDirectoryName(DatabasePath));Directory.CreateDirectory(LogDirectory);File.WriteAllText(path,new JavaScriptSerializer().Serialize(this));}
}
}
