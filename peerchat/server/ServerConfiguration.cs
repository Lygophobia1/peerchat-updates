// One persisted configuration contract shared by the headless server and its local Admin UI.
// Mutable data never depends on the executable directory or the process current directory.
using System;
using System.IO;
using System.Web.Script.Serialization;

namespace PeerChat.ServerShared {
public sealed class ServerConfiguration {
 // PublicHostname identifies the discovery service. SelfHostedPublicP2PHost is an optional
 // address for a P2P listener on this host; its port always comes from client presence.
 public string DataDirectory,ListenAddress="0.0.0.0",PublicHostname="localhost",SelfHostedPublicP2PHost="",DatabasePath,LogDirectory,CertificateThumbprint="",CertificatePath="",CertificatePassword="",ManagementPipe=""; public int Port=7443,SessionHours=336;
 public static string DefaultDataDirectory { get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),"PeerChat","Server"); } }
 public static string DefaultConfigPath { get { return Path.Combine(DefaultDataDirectory,"Config","server-config.json"); } }
 public static ServerConfiguration Load(string path){if(!File.Exists(path))return new ServerConfiguration{DataDirectory=DefaultDataDirectory};var value=new JavaScriptSerializer().Deserialize<ServerConfiguration>(File.ReadAllText(path));value.Normalize();return value;}
 public void Normalize(){if(String.IsNullOrWhiteSpace(DataDirectory))DataDirectory=DefaultDataDirectory;if(String.IsNullOrWhiteSpace(DatabasePath))DatabasePath=Path.Combine(DataDirectory,"Data","peerchat-server.db");if(String.IsNullOrWhiteSpace(LogDirectory))LogDirectory=Path.Combine(DataDirectory,"Logs");if(String.IsNullOrWhiteSpace(SelfHostedPublicP2PHost)&&!String.Equals(PublicHostname,"localhost",StringComparison.OrdinalIgnoreCase))SelfHostedPublicP2PHost=PublicHostname;}
 public void Validate(){Normalize();if(Port<1||Port>65535)throw new InvalidOperationException("TLS port must be 1 through 65535.");if(String.IsNullOrWhiteSpace(CertificateThumbprint))throw new InvalidOperationException("A TLS certificate must be configured.");}
 public void Save(string path){Normalize();Directory.CreateDirectory(Path.GetDirectoryName(path));Directory.CreateDirectory(Path.GetDirectoryName(DatabasePath));Directory.CreateDirectory(LogDirectory);File.WriteAllText(path,new JavaScriptSerializer().Serialize(this));}
}
}
