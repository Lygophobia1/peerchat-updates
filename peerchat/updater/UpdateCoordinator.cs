using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Threading;
using System.Threading.Tasks;

namespace PeerChat.Updater
{
    public sealed class UpdateCheckResult
    {
        public bool UpdateAvailable { get; set; }
        public string Reason { get; set; }
        public UpdateManifest Manifest { get; set; }
    }

    public sealed class DownloadedUpdate
    {
        public UpdateManifest Manifest { get; set; }
        public string PackagePath { get; set; }
    }

    public sealed class PreparedUpdate
    {
        public UpdateManifest Manifest { get; set; }
        public string StagedDirectory { get; set; }
    }

    // This is the small future client integration surface. The current PeerChat startup path does not call it.
    public interface IPeerChatUpdateService
    {
        Task<UpdateCheckResult> CheckForUpdate(string currentVersion, CancellationToken cancellationToken);
        Task<DownloadedUpdate> DownloadUpdate(UpdateCheckResult update, CancellationToken cancellationToken);
        PreparedUpdate PrepareUpdate(DownloadedUpdate update);
        Process LaunchUpdaterAndExit(PreparedUpdate update, string targetDirectory, int peerChatProcessId);
    }

    public sealed class PeerChatUpdateService : IPeerChatUpdateService, IDisposable
    {
        readonly UpdateFeedConfiguration configuration;
        readonly IUpdateTransport transport;
        readonly IReleaseSignatureVerifier signatureVerifier;
        readonly string updaterExecutable;

        public PeerChatUpdateService(UpdateFeedConfiguration configuration, IUpdateTransport transport,
                                     IReleaseSignatureVerifier signatureVerifier, string updaterExecutable)
        {
            this.configuration = configuration;
            this.transport = transport;
            this.signatureVerifier = signatureVerifier;
            this.updaterExecutable = updaterExecutable;
        }

        public async Task<UpdateCheckResult> CheckForUpdate(string currentVersion, CancellationToken cancellationToken)
        {
            SemanticVersion current = SemanticVersion.Parse(currentVersion);
            string json = await transport.GetManifestAsync(new Uri(configuration.ManifestUrl), cancellationToken).ConfigureAwait(false);
            UpdateManifest manifest = UpdateJson.ParseManifest(json, configuration);
            SemanticVersion offered = SemanticVersion.Parse(manifest.Version);
            if (offered.CompareTo(current) <= 0)
                return new UpdateCheckResult { UpdateAvailable = false, Reason = offered.CompareTo(current) < 0 ? "Downgrade rejected." : "Already current.", Manifest = manifest };
            if (!String.IsNullOrWhiteSpace(manifest.MinimumCurrentVersion) &&
                current.CompareTo(SemanticVersion.Parse(manifest.MinimumCurrentVersion)) < 0)
                return new UpdateCheckResult { UpdateAvailable = false, Reason = "This installation is older than the package's supported upgrade floor.", Manifest = manifest };
            return new UpdateCheckResult { UpdateAvailable = true, Reason = "A newer version is available.", Manifest = manifest };
        }

        public async Task<DownloadedUpdate> DownloadUpdate(UpdateCheckResult update, CancellationToken cancellationToken)
        {
            if (update == null || !update.UpdateAvailable || update.Manifest == null) throw new InvalidOperationException("No approved update is available to download.");
            string versionRoot = SafeVersionDirectory(update.Manifest.Version);
            string partial = Path.Combine(versionRoot, "package.zip.partial");
            string complete = Path.Combine(versionRoot, "package.zip");
            await transport.DownloadAsync(new Uri(update.Manifest.PackageUrl), partial, update.Manifest.PackageSize,
                                          configuration.MaximumPackageBytes, cancellationToken).ConfigureAwait(false);
            if (!PackageHashVerifier.VerifySha256(partial, update.Manifest.PackageSha256))
            { File.Delete(partial); throw new InvalidDataException("Downloaded package SHA-256 does not match the manifest."); }
            string signatureFailure;
            if (!signatureVerifier.Verify(update.Manifest, partial, out signatureFailure))
            { File.Delete(partial); throw new InvalidDataException(signatureFailure); }
            if (File.Exists(complete)) File.Delete(complete);
            File.Move(partial, complete);
            return new DownloadedUpdate { Manifest = update.Manifest, PackagePath = complete };
        }

        public PreparedUpdate PrepareUpdate(DownloadedUpdate update)
        {
            if (update == null || update.Manifest == null || !PackageHashVerifier.VerifySha256(update.PackagePath, update.Manifest.PackageSha256))
                throw new InvalidDataException("The staged package is missing or no longer matches its manifest.");
            string signatureFailure;
            if (!signatureVerifier.Verify(update.Manifest, update.PackagePath, out signatureFailure)) throw new InvalidDataException(signatureFailure);
            string root = SafeVersionDirectory(update.Manifest.Version);
            string extracted = Path.Combine(root, "payload-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(extracted);
            ExtractArchiveSafely(update.PackagePath, extracted, configuration.MaximumExtractedBytes, configuration.MaximumArchiveFiles);
            string executable = Path.Combine(extracted, update.Manifest.EntryExecutable.Replace('/', Path.DirectorySeparatorChar));
            if (!File.Exists(executable)) { Directory.Delete(extracted, true); throw new InvalidDataException("The package does not contain its declared entry executable."); }
            return new PreparedUpdate { Manifest = update.Manifest, StagedDirectory = extracted };
        }

        public Process LaunchUpdaterAndExit(PreparedUpdate update, string targetDirectory, int peerChatProcessId)
        {
            if (update == null || update.Manifest == null) throw new ArgumentNullException("update");
            string planPath = UpdatePlan.CreateAndSave(update, targetDirectory, peerChatProcessId, configuration.FirstLaunchTimeoutSeconds);
            Process process = Process.Start(CreateStandaloneLauncherStartInfo(updaterExecutable, planPath));
            if (process == null) throw new InvalidOperationException("The standalone updater could not be started.");
            return process;
        }

        // Kept separately so the launch boundary is regression-tested without starting a process.
        public static ProcessStartInfo CreateStandaloneLauncherStartInfo(string launcherExecutable, string planPath)
        {
            if (String.IsNullOrWhiteSpace(launcherExecutable)) throw new ArgumentException("Updater launcher path is required.", "launcherExecutable");
            if (String.IsNullOrWhiteSpace(planPath)) throw new ArgumentException("Update plan path is required.", "planPath");
            // The copied launcher must not inherit Client as its current directory: Windows treats a
            // process working directory as an open handle and then rejects Client -> backup rename.
            string launcherPath = Path.GetFullPath(launcherExecutable);
            string launcherDirectory = Path.GetDirectoryName(launcherPath);
            return new ProcessStartInfo(launcherPath,
                "apply --plan " + Quote(planPath)) {
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    WorkingDirectory = launcherDirectory
                };
        }

        string SafeVersionDirectory(string version)
        {
            SemanticVersion parsed = SemanticVersion.Parse(version);
            string path = Path.Combine(configuration.StagingRoot, parsed.ToString());
            Directory.CreateDirectory(path); return path;
        }

        static void ExtractArchiveSafely(string archive, string destination, long maximumBytes, int maximumFiles)
        {
            string root = Path.GetFullPath(destination).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            long total = 0; int count = 0;
            using (ZipArchive zip = ZipFile.OpenRead(archive))
            foreach (ZipArchiveEntry entry in zip.Entries)
            {
                if (++count > maximumFiles) throw new InvalidDataException("Update archive contains too many files.");
                string output = Path.GetFullPath(Path.Combine(root, entry.FullName.Replace('/', Path.DirectorySeparatorChar)));
                if (!output.StartsWith(root, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Update archive contains an unsafe path.");
                if (entry.FullName.EndsWith("/", StringComparison.Ordinal)) { Directory.CreateDirectory(output); continue; }
                total += entry.Length;
                if (total > maximumBytes) throw new InvalidDataException("Expanded update exceeds its configured size limit.");
                Directory.CreateDirectory(Path.GetDirectoryName(output));
                using (Stream input = entry.Open())
                using (FileStream file = new FileStream(output, FileMode.CreateNew, FileAccess.Write, FileShare.None)) input.CopyTo(file);
            }
        }

        static string Quote(string value) { return "\"" + value.Replace("\"", "\\\"") + "\""; }
        public void Dispose() { IDisposable disposable = transport as IDisposable; if (disposable != null) disposable.Dispose(); }
    }
}
