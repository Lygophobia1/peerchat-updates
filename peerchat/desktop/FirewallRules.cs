using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;

namespace PeerChatDesktop {
// Keeps the Windows Firewall boundary outside the view code. This only grants inbound
// TCP access to the current packaged client on Private networks; it never opens Public.
internal sealed class FirewallResult { public bool Ok,PermissionDenied,Changed; public string Detail; }

internal static class FirewallRules {
    internal const string RuleName="PeerChat P2P (Private)";

    internal static string ProgramPath(){
        try{return Path.GetFullPath(Process.GetCurrentProcess().MainModule.FileName);}
        catch{try{return Path.GetFullPath(Assembly.GetEntryAssembly().Location);}catch{return null;}}
    }

    static string Query(){
        try{
            var info=new ProcessStartInfo("netsh.exe","advfirewall firewall show rule name=\""+RuleName+"\" verbose"){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
            using(var process=Process.Start(info)){string output=process.StandardOutput.ReadToEnd();string error=process.StandardError.ReadToEnd();process.WaitForExit();return output+"\n"+error;}
        }catch{return "";}
    }

    // Netsh prints an English "No rules match" message rather than returning empty output.
    // Treat a rule as present only after seeing its actual verbose rule header.
    internal static bool RuleExists(string output){return !String.IsNullOrEmpty(output)&&Regex.IsMatch(output,"^\\s*Rule Name\\s*:",RegexOptions.IgnoreCase|RegexOptions.Multiline);}

    // A temporary listener uses a different OS-selected port each time. The
    // installation rule is deliberately application-scoped rather than
    // port-scoped: exact executable, inbound TCP and Private only.
    internal static bool RuleMatchesProgramPrivateTcp(string output,string program){
        if(!RuleExists(output)||String.IsNullOrEmpty(program))return false;
        string normalized=output.Replace("\r","");
        return Regex.IsMatch(normalized,"^\\s*Enabled:\\s*Yes\\s*$",RegexOptions.IgnoreCase|RegexOptions.Multiline)&&
            Regex.IsMatch(normalized,"^\\s*Direction:\\s*In\\s*$",RegexOptions.IgnoreCase|RegexOptions.Multiline)&&
            Regex.IsMatch(normalized,"^\\s*Profiles:\\s*Private\\s*$",RegexOptions.IgnoreCase|RegexOptions.Multiline)&&
            Regex.IsMatch(normalized,"^\\s*Protocol:\\s*TCP\\s*$",RegexOptions.IgnoreCase|RegexOptions.Multiline)&&
            Regex.IsMatch(normalized,"^\\s*Action:\\s*Allow\\s*$",RegexOptions.IgnoreCase|RegexOptions.Multiline)&&
            Regex.IsMatch(normalized,"^\\s*LocalPort:\\s*(Any|\\*)\\s*$",RegexOptions.IgnoreCase|RegexOptions.Multiline)&&
            Regex.IsMatch(normalized,"Program:\\s*"+Regex.Escape(program)+"\\s*$",RegexOptions.IgnoreCase|RegexOptions.Multiline);
    }

    // Retained only for compatibility with the focused parser test and to
    // demonstrate that a historical exact-port rule is no longer acceptable.
    internal static bool RuleMatches(string output,ushort port,string program){return port>0&&RuleMatchesProgramPrivateTcp(output,program);}

    // This file is installation metadata only. The live Windows Firewall rule remains
    // authoritative and is queried silently on every process start before elevation.
    static void SaveInstallState(string installRoot,string program){
        if(String.IsNullOrEmpty(installRoot))return;
        try{
            Directory.CreateDirectory(installRoot);
            string path=Path.Combine(installRoot,"firewall-state.dat"),temporary=path+".new";
            string content="version=2\r\nscope=program-private-tcp\r\nprogram="+program+"\r\nverifiedUtc="+DateTime.UtcNow.ToString("o")+"\r\n";
            File.WriteAllText(temporary,content,new UTF8Encoding(false));
            if(File.Exists(path))File.Replace(temporary,path,null);else File.Move(temporary,path);
        }catch{} // Provisioning must not fail merely because status metadata is unavailable.
    }

    internal static FirewallResult EnsurePrivateProgramRule(string installRoot=null){
        var result=new FirewallResult();
        string program=ProgramPath();
        if(String.IsNullOrEmpty(program)){result.PermissionDenied=true;result.Detail="Could not determine the packaged PeerChat application path.";return result;}
        string existing=Query();
        if(RuleMatchesProgramPrivateTcp(existing,program)){
            result.Ok=true;result.Detail="Existing installation firewall rule reused.";
            SaveInstallState(installRoot,program);return result;
        }
        string quotedProgram="\""+program.Replace("\"","\\\"")+"\"";
        // Reusing the historical rule name means an old exact-port rule is
        // migrated in place instead of leaving two confusing allowances.
        string command="advfirewall firewall "+(RuleExists(existing)?"set rule name=\""+RuleName+"\" new dir=in action=allow enable=yes profile=private protocol=TCP localport=any program="+quotedProgram:"add rule name=\""+RuleName+"\" dir=in action=allow enable=yes profile=private protocol=TCP localport=any program="+quotedProgram);
        try{
            var info=new ProcessStartInfo("netsh.exe",command){UseShellExecute=true,Verb="runas",WindowStyle=ProcessWindowStyle.Hidden};
            using(var process=Process.Start(info)){
                if(!process.WaitForExit(10000)){try{process.Kill();}catch{}result.PermissionDenied=true;result.Detail="Windows Firewall did not respond to the elevation request.";return result;}
                result.Ok=process.ExitCode==0&&RuleMatchesProgramPrivateTcp(Query(),program);
                if(result.Ok){result.Changed=true;result.Detail="Installation firewall rule migrated or updated for temporary Private TCP P2P.";SaveInstallState(installRoot,program);}
                else result.Detail="Windows Firewall did not accept a valid Private TCP PeerChat rule.";
            }
        }catch(Win32Exception error){result.PermissionDenied=error.NativeErrorCode==1223||error.NativeErrorCode==740;result.Detail=result.PermissionDenied?"Approve the Windows security prompt to allow inbound PeerChat connections on Private networks.":"Windows Firewall could not be started: "+error.Message;}
        catch(Exception error){result.Detail="Windows Firewall setup failed: "+error.Message;}
        return result;
    }

    [Obsolete("Temporary reliable listeners require the program-scoped Private TCP rule.")]
    internal static FirewallResult EnsurePrivateRule(ushort port,string installRoot=null){return EnsurePrivateProgramRule(installRoot);}
}
}
