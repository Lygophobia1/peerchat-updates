using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Threading;

namespace PeerChat.Updater
{
    public enum UpdatePhase { Prepared, ReplacementCopied, TargetBackedUp, ReplacementActivated, Launching, Confirmed, RolledBack }

    public sealed class UpdatePlan
    {
        public int SchemaVersion { get; set; }
        public string CurrentVersion { get; set; }
        public string TargetVersion { get; set; }
        public string TargetDirectory { get; set; }
        public string StagedDirectory { get; set; }
        public string BackupDirectory { get; set; }
        public string ReplacementDirectory { get; set; }
        public string EntryExecutable { get; set; }
        public string HealthTokenPath { get; set; }
        public int ProcessIdToWaitFor { get; set; }
        public int FirstLaunchTimeoutSeconds { get; set; }
        public UpdatePhase Phase { get; set; }
        public string PlanPath { get; set; }

        public static string CreateAndSave(PreparedUpdate update, string targetDirectory, int processId, int timeoutSeconds)
        {
            string target = Path.GetFullPath(targetDirectory).TrimEnd(Path.DirectorySeparatorChar);
            if (!Directory.Exists(target)) throw new DirectoryNotFoundException("Update target directory does not exist: " + target);
            string parent = Path.GetDirectoryName(target), id = Guid.NewGuid().ToString("N");
            UpdatePlan plan = new UpdatePlan {
                SchemaVersion = 1,
                CurrentVersion = PeerChat.VersionInfo.Display,
                TargetVersion = update.Manifest.Version,
                TargetDirectory = target,
                StagedDirectory = Path.GetFullPath(update.StagedDirectory),
                BackupDirectory = Path.Combine(parent, ".peerchat-backup-" + id),
                ReplacementDirectory = Path.Combine(parent, ".peerchat-new-" + id),
                EntryExecutable = update.Manifest.EntryExecutable.Replace('/', Path.DirectorySeparatorChar),
                HealthTokenPath = Path.Combine(parent, ".peerchat-health-" + id),
                ProcessIdToWaitFor = processId,
                FirstLaunchTimeoutSeconds = timeoutSeconds,
                Phase = UpdatePhase.Prepared
            };
            if (SemanticVersion.Parse(plan.TargetVersion).CompareTo(SemanticVersion.Parse(plan.CurrentVersion)) <= 0)
                throw new InvalidOperationException("Updater refused to prepare a downgrade or same-version replacement.");
            string planPath = Path.Combine(parent, ".peerchat-update-" + id + ".json");
            plan.PlanPath = planPath; Save(plan); return planPath;
        }

        public static UpdatePlan Load(string path)
        {
            UpdatePlan plan = UpdateJson.DeserializeFile<UpdatePlan>(path);
            if (plan == null || plan.SchemaVersion != 1) throw new InvalidDataException("Update plan is invalid.");
            plan.PlanPath = Path.GetFullPath(path);
            plan.Validate(); return plan;
        }

        public void Validate()
        {
            if (SemanticVersion.Parse(TargetVersion).CompareTo(SemanticVersion.Parse(CurrentVersion)) <= 0)
                throw new InvalidDataException("Update plan attempts a downgrade or same-version replacement.");
            TargetDirectory = Path.GetFullPath(TargetDirectory).TrimEnd(Path.DirectorySeparatorChar);
            StagedDirectory = Path.GetFullPath(StagedDirectory).TrimEnd(Path.DirectorySeparatorChar);
            BackupDirectory = Path.GetFullPath(BackupDirectory).TrimEnd(Path.DirectorySeparatorChar);
            ReplacementDirectory = Path.GetFullPath(ReplacementDirectory).TrimEnd(Path.DirectorySeparatorChar);
            HealthTokenPath = Path.GetFullPath(HealthTokenPath);
            string parent = Path.GetDirectoryName(TargetDirectory) + Path.DirectorySeparatorChar;
            if (!BackupDirectory.StartsWith(parent, StringComparison.OrdinalIgnoreCase) ||
                !ReplacementDirectory.StartsWith(parent, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Backup and replacement must be siblings of the installation directory.");
            UpdateManifest.ValidateRelativePath(EntryExecutable, "Update entry executable");
            if (FirstLaunchTimeoutSeconds < 5 || FirstLaunchTimeoutSeconds > 300) throw new InvalidDataException("Invalid first-launch timeout.");
        }

        public static void Save(UpdatePlan plan)
        {
            string temp = plan.PlanPath + ".tmp";
            File.WriteAllText(temp, UpdateJson.Serialize(plan));
            if (File.Exists(plan.PlanPath)) File.Replace(temp, plan.PlanPath, null); else File.Move(temp, plan.PlanPath);
        }
    }

    public sealed class StandaloneInstaller
    {
        readonly Action<string> log;
        readonly Action<string, string> moveDirectory;
        const int RenameAttemptCount = 6;
        static readonly int[] RenameRetryDelaysMilliseconds = new[] { 125, 250, 500, 1000, 2000 };

        public StandaloneInstaller(Action<string> log) : this(log, Directory.Move) { }

        // The move seam is deliberately narrow: it exists to regression-test a transient Windows
        // sharing lock without changing the update plan, validation, or rollback contract.
        public StandaloneInstaller(Action<string> log, Action<string, string> moveDirectory)
        {
            if (moveDirectory == null) throw new ArgumentNullException("moveDirectory");
            this.log = log ?? delegate { };
            this.moveDirectory = moveDirectory;
        }

        public int Apply(UpdatePlan plan)
        {
            plan.Validate();
            try
            {
                WaitForProcess(plan.ProcessIdToWaitFor, TimeSpan.FromMinutes(2));
                RecoverInterruptedTransaction(plan);
                if (!Directory.Exists(plan.StagedDirectory)) throw new DirectoryNotFoundException("Prepared update payload is missing.");
                CopyDirectory(plan.StagedDirectory, plan.ReplacementDirectory);
                plan.Phase = UpdatePhase.ReplacementCopied; UpdatePlan.Save(plan);

                MoveCurrentClientToBackup(plan);
                plan.Phase = UpdatePhase.TargetBackedUp; UpdatePlan.Save(plan);
                moveDirectory(plan.ReplacementDirectory, plan.TargetDirectory);
                plan.Phase = UpdatePhase.ReplacementActivated; UpdatePlan.Save(plan);

                string executable = Path.Combine(plan.TargetDirectory, plan.EntryExecutable);
                if (!File.Exists(executable)) throw new FileNotFoundException("Updated entry executable is missing.", executable);
                if (File.Exists(plan.HealthTokenPath)) File.Delete(plan.HealthTokenPath);
                Process child = Process.Start(new ProcessStartInfo(executable, "--peerchat-update-health-token " + Quote(plan.HealthTokenPath)) { UseShellExecute = false });
                if (child == null) throw new InvalidOperationException("Updated PeerChat process did not start.");
                plan.Phase = UpdatePhase.Launching; UpdatePlan.Save(plan);
                if (!WaitForHealth(child, plan.HealthTokenPath, TimeSpan.FromSeconds(plan.FirstLaunchTimeoutSeconds)))
                {
                    try { if (!child.HasExited) child.Kill(); } catch { }
                    throw new InvalidOperationException("Updated PeerChat did not confirm a healthy first launch in time.");
                }
                plan.Phase = UpdatePhase.Confirmed; UpdatePlan.Save(plan);
                log("Update confirmed healthy. The rollback copy is retained for bounded cleanup.");
                return 0;
            }
            catch (Exception error)
            {
                log("Update failed: " + error.GetType().Name + ": " + error.Message);
                try { RollBack(plan, true); }
                catch (Exception rollbackError) { log("Rollback failed: " + rollbackError.GetType().Name + ": " + rollbackError.Message); return 3; }
                return 2;
            }
        }

        public void RecoverInterruptedTransaction(UpdatePlan plan)
        {
            if (plan.Phase == UpdatePhase.Prepared) return;
            if (plan.Phase == UpdatePhase.ReplacementCopied && Directory.Exists(plan.TargetDirectory))
            { DeleteDirectoryIfPresent(plan.ReplacementDirectory); plan.Phase = UpdatePhase.Prepared; UpdatePlan.Save(plan); return; }
            if ((plan.Phase == UpdatePhase.TargetBackedUp || plan.Phase == UpdatePhase.ReplacementActivated || plan.Phase == UpdatePhase.Launching) &&
                Directory.Exists(plan.BackupDirectory))
            { RollBack(plan, false); }
        }

        public void RollBack(UpdatePlan plan, bool restartPrevious)
        {
            if (Directory.Exists(plan.BackupDirectory))
            {
                string failed = plan.TargetDirectory + ".failed-" + DateTime.UtcNow.ToString("yyyyMMddHHmmss");
                if (Directory.Exists(plan.TargetDirectory)) moveDirectory(plan.TargetDirectory, failed);
                moveDirectory(plan.BackupDirectory, plan.TargetDirectory);
                plan.Phase = UpdatePhase.RolledBack; UpdatePlan.Save(plan);
                log("Previous PeerChat installation restored.");
                if (restartPrevious)
                {
                    string executable = Path.Combine(plan.TargetDirectory, plan.EntryExecutable);
                    if (File.Exists(executable)) Process.Start(new ProcessStartInfo(executable) { UseShellExecute = true });
                }
            }
            DeleteDirectoryIfPresent(plan.ReplacementDirectory);
        }

        static bool WaitForHealth(Process child, string tokenPath, TimeSpan timeout)
        {
            Stopwatch watch = Stopwatch.StartNew();
            while (watch.Elapsed < timeout)
            {
                if (File.Exists(tokenPath)) return true;
                if (child.HasExited) return false;
                Thread.Sleep(100);
            }
            return false;
        }

        void MoveCurrentClientToBackup(UpdatePlan plan)
        {
            for (int attempt = 1; attempt <= RenameAttemptCount; ++attempt)
            {
                try
                {
                    log("Moving current Client directory to rollback backup; attempt " + attempt + " of " + RenameAttemptCount + ".");
                    moveDirectory(plan.TargetDirectory, plan.BackupDirectory);
                    log("Current Client directory moved to rollback backup on attempt " + attempt + ".");
                    return;
                }
                catch (IOException error)
                {
                    if (!IsTransientSharingLock(error) || attempt == RenameAttemptCount)
                    {
                        log("Current Client-to-backup rename failed on attempt " + attempt + ": " + error.GetType().Name + ": " + error.Message);
                        throw;
                    }
                    int delay = RenameRetryDelaysMilliseconds[attempt - 1];
                    log("Current Client-to-backup rename hit a transient sharing lock on attempt " + attempt + "; retrying in " + delay + " ms.");
                    Thread.Sleep(delay);
                }
            }
        }

        static bool IsTransientSharingLock(IOException error)
        {
            int code = error.HResult & 0xffff;
            return code == 32 || code == 33; // ERROR_SHARING_VIOLATION / ERROR_LOCK_VIOLATION
        }

        void WaitForProcess(int processId, TimeSpan timeout)
        {
            if (processId <= 0) return;
            try
            {
                using (Process process = Process.GetProcessById(processId))
                {
                    log("Waiting for original PeerChat process " + processId + " to exit before replacement.");
                    if (!process.WaitForExit((int)timeout.TotalMilliseconds)) throw new TimeoutException("PeerChat did not exit before update timeout.");
                    if (!process.HasExited) throw new TimeoutException("PeerChat process is still running before replacement.");
                    log("Original PeerChat process " + processId + " exited before replacement.");
                }
            }
            catch (ArgumentException) { log("Original PeerChat process " + processId + " was already exited before replacement."); }
        }

        static void CopyDirectory(string source, string destination)
        {
            if (Directory.Exists(destination)) Directory.Delete(destination, true);
            Directory.CreateDirectory(destination);
            foreach (string directory in Directory.GetDirectories(source, "*", SearchOption.AllDirectories))
                Directory.CreateDirectory(Path.Combine(destination, directory.Substring(source.Length).TrimStart(Path.DirectorySeparatorChar)));
            foreach (string file in Directory.GetFiles(source, "*", SearchOption.AllDirectories))
            {
                string relative = file.Substring(source.Length).TrimStart(Path.DirectorySeparatorChar);
                string target = Path.Combine(destination, relative); Directory.CreateDirectory(Path.GetDirectoryName(target)); File.Copy(file, target, true);
            }
        }

        static void DeleteDirectoryIfPresent(string path) { if (Directory.Exists(path)) Directory.Delete(path, true); }
        static string Quote(string value) { return "\"" + value.Replace("\"", "\\\"") + "\""; }
    }

    public static class UpdateHealthReporter
    {
        public static bool MarkHealthyFromCommandLine(string[] arguments)
        {
            for (int i = 0; i + 1 < arguments.Length; ++i)
                if (arguments[i] == "--peerchat-update-health-token")
                {
                    string path = Path.GetFullPath(arguments[i + 1]);
                    File.WriteAllText(path, DateTime.UtcNow.ToString("O")); return true;
                }
            return false;
        }
    }

    public static class StagingCleanup
    {
        public static int RemoveOldEntries(string stagingRoot, TimeSpan maximumAge)
        {
            if (!Directory.Exists(stagingRoot)) return 0;
            int removed = 0; DateTime cutoff = DateTime.UtcNow - maximumAge;
            foreach (string directory in Directory.GetDirectories(stagingRoot))
            {
                if (File.Exists(Path.Combine(directory, ".active"))) continue;
                if (Directory.GetLastWriteTimeUtc(directory) < cutoff)
                { try { Directory.Delete(directory, true); ++removed; } catch (IOException) { } catch (UnauthorizedAccessException) { } }
            }
            return removed;
        }
    }
}
