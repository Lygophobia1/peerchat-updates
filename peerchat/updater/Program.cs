using System;
using System.IO;

namespace PeerChat.Updater
{
    static class Program
    {
        static int Main(string[] args)
        {
            try
            {
                // This is normally redundant with the launcher's explicit WorkingDirectory, but it
                // also protects direct/manual invocation from holding the Client directory open.
                string updaterDirectory = Path.GetDirectoryName(Path.GetFullPath(typeof(Program).Assembly.Location));
                if (!String.IsNullOrWhiteSpace(updaterDirectory)) Environment.CurrentDirectory = updaterDirectory;
                if (args.Length == 3 && args[0] == "apply" && args[1] == "--plan")
                {
                    string logPath = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(args[2])), "PeerChat.Updater.log");
                    Action<string> log = message => UpdaterLog.Write(logPath, "INFO", message);
                    return new StandaloneInstaller(log).Apply(UpdatePlan.Load(args[2]));
                }
                if (args.Length == 3 && args[0] == "cleanup" && args[1] == "--config")
                {
                    UpdateFeedConfiguration configuration = UpdateFeedConfiguration.Load(args[2]);
                    Console.WriteLine("Removed " + StagingCleanup.RemoveOldEntries(configuration.StagingRoot, TimeSpan.FromDays(14)) + " old staging directories.");
                    return 0;
                }
                Console.Error.WriteLine("Usage: PeerChat.Updater.exe apply --plan <update-plan.json> | cleanup --config <updater-config.json>");
                return 1;
            }
            catch (Exception error)
            {
                Console.Error.WriteLine("Updater failed: " + error.GetType().Name + ": " + error.Message);
                return 2;
            }
        }
    }

    static class UpdaterLog
    {
        const long MaximumLogBytes = 1024 * 1024;
        public static void Write(string path, string severity, string message)
        {
            // Messages are operational state only; callers must never pass credentials, tokens, or private keys.
            string line = DateTime.UtcNow.ToString("O") + " " + severity + " " + (message ?? "").Replace('\r', ' ').Replace('\n', ' ');
            try
            {
                if (File.Exists(path) && new FileInfo(path).Length >= MaximumLogBytes)
                {
                    string previous = path + ".1"; if (File.Exists(previous)) File.Delete(previous); File.Move(path, previous);
                }
                File.AppendAllText(path, line + Environment.NewLine);
            }
            catch { }
            Console.WriteLine(line);
        }
    }
}
