using System;
using System.Diagnostics;
using PeerChatDesktop;

class FirewallRuleTest {
    static void Require(bool value,string message){if(!value)throw new Exception(message);}

    static int Main(){
        try{
            const string program=@"C:\PeerChat\PeerChat.Desktop.exe";
            string missing="No rules match the specified criteria.";
            string valid="Rule Name: PeerChat P2P (Private)\r\nEnabled: Yes\r\nDirection: In\r\nProfiles: Private\r\nLocalPort: 7777\r\nProgram: "+program+"\r\n";
            Require(!FirewallRules.RuleExists(missing),"Netsh no-rule text was treated as a rule.");
            Require(FirewallRules.RuleExists(valid),"Verbose rule header was not recognized.");
            Require(FirewallRules.RuleMatches(valid,7777,program),"Valid Private executable rule did not match.");
            Require(!FirewallRules.RuleMatches(valid,7778,program),"Old listener port was treated as current.");
            Require(!FirewallRules.RuleMatches(valid,7777,@"C:\Other\PeerChat.Desktop.exe"),"Stale executable path was treated as current.");
            Require(!FirewallRules.RuleMatches(valid.Replace("Profiles: Private","Profiles: Public"),7777,program),"Public rule was accepted.");
            // If the developer machine has the rule installed, verify that its exact Netsh
            // rendering is recognized too. A clean CI machine legitimately has no such rule.
            var info=new ProcessStartInfo("netsh.exe","advfirewall firewall show rule name=\""+FirewallRules.RuleName+"\" verbose"){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true};
            using(var process=Process.Start(info)){
                string installed=process.StandardOutput.ReadToEnd();process.WaitForExit();
                string installedProgram=Environment.GetEnvironmentVariable("PEERCHAT_FIREWALL_PROGRAM");
                if(FirewallRules.RuleExists(installed)&&!String.IsNullOrEmpty(installedProgram)){
                    Require(FirewallRules.RuleMatches(installed,7777,installedProgram),"Installed firewall rule was not recognized for its packaged executable.");
                }
            }
            Console.WriteLine("Firewall rule tests passed.");
            return 0;
        }catch(Exception error){Console.Error.WriteLine("FAIL: "+error.Message);return 1;}
    }
}
