using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using PeerChat.Updater;

static class UpdaterTests
{
    static int assertions;

    static void Assert(bool condition, string message)
    {
        ++assertions;
        if (!condition) throw new Exception("Assertion failed: " + message);
    }

    static void Throws<T>(Action action, string message) where T : Exception
    {
        ++assertions;
        try { action(); }
        catch (T) { return; }
        throw new Exception("Expected " + typeof(T).Name + ": " + message);
    }

    static UpdateFeedConfiguration Configuration(string root)
    {
        return new UpdateFeedConfiguration {
            ManifestUrl = "https://updates.peerchat.test/beta/manifest.json", Channel = "beta", StagingRoot = root,
            RequireSignature = false, MaximumPackageBytes = 1024 * 1024, MaximumExtractedBytes = 2 * 1024 * 1024,
            MaximumArchiveFiles = 100, FirstLaunchTimeoutSeconds = 10
        };
    }

    static UpdateManifest Manifest(string version, byte[] package)
    {
        return new UpdateManifest {
            SchemaVersion = 1, Product = "PeerChat", Channel = "beta", Version = version,
            MinimumCurrentVersion = "0.3.0-beta", PackageUrl = "https://updates.peerchat.test/beta/package.zip",
            PackageSha256 = Hash(package), PackageSize = package.Length, EntryExecutable = "PeerChat.Desktop.exe",
            PublishedUtc = "2026-09-11T12:00:00Z"
        };
    }

    static string Hash(byte[] bytes)
    {
        using (SHA256 sha = SHA256.Create()) return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "").ToLowerInvariant();
    }

    static void TestSemanticVersions()
    {
        Assert(UpdateFeedConfiguration.CreateDefaults().Channel == "release", "default update channel is release");
        Assert(SemanticVersion.Parse("0.3.0-beta").CompareTo(SemanticVersion.Parse("0.3.0")) < 0, "pre-release sorts below release");
        Assert(SemanticVersion.Parse("1.0.0-beta.2").CompareTo(SemanticVersion.Parse("1.0.0-beta.11")) < 0, "numeric identifiers compare numerically");
        Assert(SemanticVersion.Parse("1.0.0-beta.99999999999999999999").CompareTo(SemanticVersion.Parse("1.0.0-beta.100000000000000000000")) < 0, "large numeric identifiers compare without integer overflow");
        Assert(SemanticVersion.Parse("v2.0.0+build.4").CompareTo(SemanticVersion.Parse("2.0.0+build.5")) == 0, "build metadata does not affect precedence");
        Throws<FormatException>(() => SemanticVersion.Parse("1.02.3"), "leading zeros rejected");
        Throws<FormatException>(() => SemanticVersion.Parse("1.0"), "incomplete versions rejected");
    }

    static void TestManifestParsing(string root)
    {
        byte[] package = Encoding.UTF8.GetBytes("package"); UpdateFeedConfiguration config = Configuration(root);
        string json = UpdateJson.Serialize(Manifest("0.4.0-beta.1", package));
        UpdateManifest parsed = UpdateJson.ParseManifest(json, config);
        Assert(parsed.Version == "0.4.0-beta.1", "valid manifest parsed");
        string malformed = json.Replace("https://updates.peerchat.test", "http://updates.peerchat.test");
        Throws<InvalidDataException>(() => UpdateJson.ParseManifest(malformed, config), "non-HTTPS package rejected");
        UpdateManifest unsafeManifest = Manifest("0.4.0", package); unsafeManifest.EntryExecutable = "..\\escape.exe";
        Throws<InvalidDataException>(() => UpdateJson.ParseManifest(UpdateJson.Serialize(unsafeManifest), config), "unsafe entry path rejected");
    }

    static void TestHashVerification(string root)
    {
        string path = Path.Combine(root, "hash.bin"); File.WriteAllBytes(path, Encoding.UTF8.GetBytes("verified"));
        Assert(PackageHashVerifier.VerifySha256(path, Hash(File.ReadAllBytes(path))), "matching SHA-256 accepted");
        Assert(!PackageHashVerifier.VerifySha256(path, new string('0', 64)), "wrong SHA-256 rejected");
    }

    static TrustedReleasePublicKey PublicKey(string keyId, RSACryptoServiceProvider rsa, bool revoked)
    {
        RSAParameters key = rsa.ExportParameters(false);
        return new TrustedReleasePublicKey(keyId, TrustedReleaseKeys.RsaSha256Pkcs1Algorithm,
            Convert.ToBase64String(key.Modulus), Convert.ToBase64String(key.Exponent), revoked);
    }

    static void Sign(UpdateManifest manifest, RSACryptoServiceProvider rsa, string keyId)
    {
        manifest.Signature = new UpdateSignature {
            Algorithm = TrustedReleaseKeys.RsaSha256Pkcs1Algorithm,
            KeyId = keyId
        };
        using (SHA256 sha256 = SHA256.Create())
            manifest.Signature.Value = Convert.ToBase64String(rsa.SignData(manifest.GetSignaturePayload(), sha256));
    }

    static void TestReleaseSignatures(string root)
    {
        string packagePath = Path.Combine(root, "signed-package.zip");
        byte[] package = Encoding.UTF8.GetBytes("exact signed release package bytes");
        File.WriteAllBytes(packagePath, package);
        UpdateManifest manifest = Manifest("4.0.0", package); manifest.Channel = "release";
        using (RSACryptoServiceProvider first = new RSACryptoServiceProvider(2048))
        using (RSACryptoServiceProvider rotated = new RSACryptoServiceProvider(2048))
        {
            first.PersistKeyInCsp = false; rotated.PersistKeyInCsp = false;
            IList<TrustedReleasePublicKey> keys = new List<TrustedReleasePublicKey> {
                PublicKey("release-key-1", first, false), PublicKey("release-key-2", rotated, false)
            };
            SignatureBoundaryVerifier verifier = new SignatureBoundaryVerifier(true, keys);
            string failure;
            Assert(!verifier.Verify(manifest, packagePath, out failure) && failure.IndexOf("unsigned", StringComparison.OrdinalIgnoreCase) >= 0,
                "required Release signature fails closed when absent");
            Sign(manifest, first, "release-key-1");
            Assert(verifier.Verify(manifest, packagePath, out failure), "valid trusted Release signature accepted");
            string originalUrl = manifest.PackageUrl; manifest.PackageUrl += "-tampered";
            Assert(!verifier.Verify(manifest, packagePath, out failure), "signed manifest metadata tampering rejected");
            manifest.PackageUrl = originalUrl;
            Sign(manifest, rotated, "release-key-2");
            Assert(verifier.Verify(manifest, packagePath, out failure), "rotated trusted Release key accepted during overlap");
            manifest.Signature.KeyId = "unknown-key";
            Assert(!verifier.Verify(manifest, packagePath, out failure) && failure.IndexOf("unknown", StringComparison.OrdinalIgnoreCase) >= 0,
                "unknown Release key rejected");
            Sign(manifest, first, "release-key-1");
            manifest.Signature.Value = Convert.ToBase64String(Encoding.UTF8.GetBytes("not a signature"));
            Assert(!verifier.Verify(manifest, packagePath, out failure), "invalid Release signature rejected");
            SignatureBoundaryVerifier revoked = new SignatureBoundaryVerifier(true,
                new List<TrustedReleasePublicKey> { PublicKey("release-key-1", first, true) });
            Sign(manifest, first, "release-key-1");
            Assert(!revoked.Verify(manifest, packagePath, out failure) && failure.IndexOf("revoked", StringComparison.OrdinalIgnoreCase) >= 0,
                "revoked Release key rejected");
        }
    }

    static void TestInterruptedDownload(string root)
    {
        byte[] package = Encoding.UTF8.GetBytes("complete update bytes"); UpdateManifest manifest = Manifest("0.4.0-beta.1", package);
        InterruptingTransport transport = new InterruptingTransport(UpdateJson.Serialize(manifest), package);
        using (PeerChatUpdateService service = new PeerChatUpdateService(Configuration(root), transport, new SignatureBoundaryVerifier(false), "updater.exe"))
        {
            UpdateCheckResult check = service.CheckForUpdate("0.3.0-beta", CancellationToken.None).GetAwaiter().GetResult();
            try { service.DownloadUpdate(check, CancellationToken.None).GetAwaiter().GetResult(); throw new Exception("Expected interrupted download."); }
            catch (IOException) { }
            string partial = Path.Combine(root, manifest.Version, "package.zip.partial");
            Assert(File.Exists(partial) && new FileInfo(partial).Length > 0, "interrupted download retains partial file");
            DownloadedUpdate completed = service.DownloadUpdate(check, CancellationToken.None).GetAwaiter().GetResult();
            Assert(File.Exists(completed.PackagePath), "retry completes package");
            Assert(PackageHashVerifier.VerifySha256(completed.PackagePath, manifest.PackageSha256), "retry package hash matches");
        }
    }

    static void TestDowngradeRejection(string root)
    {
        byte[] package = Encoding.UTF8.GetBytes("old"); UpdateManifest manifest = Manifest("0.2.0", package); manifest.MinimumCurrentVersion = "0.1.0";
        using (PeerChatUpdateService service = new PeerChatUpdateService(Configuration(root), new InterruptingTransport(UpdateJson.Serialize(manifest), package), new SignatureBoundaryVerifier(false), "updater.exe"))
        {
            UpdateCheckResult result = service.CheckForUpdate("0.3.0-beta", CancellationToken.None).GetAwaiter().GetResult();
            Assert(!result.UpdateAvailable && result.Reason.IndexOf("Downgrade", StringComparison.OrdinalIgnoreCase) >= 0, "downgrade rejected before download");
        }
    }

    static void TestRollbackRecovery(string root)
    {
        string parent = Path.Combine(root, "transaction"); string target = Path.Combine(parent, "Client"); string staged = Path.Combine(parent, "staged");
        Directory.CreateDirectory(target); Directory.CreateDirectory(staged);
        File.WriteAllText(Path.Combine(target, "PeerChat.Desktop.exe"), "old"); File.WriteAllText(Path.Combine(staged, "PeerChat.Desktop.exe"), "new");
        PreparedUpdate prepared = new PreparedUpdate { Manifest = new UpdateManifest { Version = "4.0.1-dev.1", EntryExecutable = "PeerChat.Desktop.exe" }, StagedDirectory = staged };
        string planPath = UpdatePlan.CreateAndSave(prepared, target, 0, 10); UpdatePlan plan = UpdatePlan.Load(planPath);
        Directory.Move(plan.TargetDirectory, plan.BackupDirectory); plan.Phase = UpdatePhase.TargetBackedUp; UpdatePlan.Save(plan);
        new StandaloneInstaller(delegate { }).RecoverInterruptedTransaction(plan);
        Assert(File.Exists(Path.Combine(target, "PeerChat.Desktop.exe")), "interrupted transaction restores old target");
        Assert(File.ReadAllText(Path.Combine(target, "PeerChat.Desktop.exe")) == "old", "rollback restores original bytes");
        Assert(UpdatePlan.Load(planPath).Phase == UpdatePhase.RolledBack, "rollback state journaled");
        Throws<InvalidOperationException>(() => {
            PreparedUpdate old = new PreparedUpdate { Manifest = new UpdateManifest { Version = "0.2.0", EntryExecutable = "PeerChat.Desktop.exe" }, StagedDirectory = staged };
            UpdatePlan.CreateAndSave(old, target, 0, 10);
        }, "plan creation rejects downgrade");
    }

    static void TestStandaloneApplyAndFirstLaunchRollback(string root, string healthProbe)
    {
        string updaterCore = Path.Combine(Path.GetDirectoryName(healthProbe), "PeerChat.Updater.Core.dll");
        string success = Path.Combine(root, "apply-success"); Directory.CreateDirectory(success);
        string successTarget = Path.Combine(success, "Client"), successStage = Path.Combine(success, "stage");
        Directory.CreateDirectory(successTarget); Directory.CreateDirectory(successStage);
        File.WriteAllText(Path.Combine(successTarget, "old.marker"), "old"); File.Copy(healthProbe, Path.Combine(successStage, "PeerChat.Desktop.exe"));
        File.Copy(updaterCore, Path.Combine(successStage, "PeerChat.Updater.Core.dll"));
        PreparedUpdate prepared = new PreparedUpdate { Manifest = new UpdateManifest { Version = "4.0.1-dev.1", EntryExecutable = "PeerChat.Desktop.exe" }, StagedDirectory = successStage };
        UpdatePlan successPlan = UpdatePlan.Load(UpdatePlan.CreateAndSave(prepared, successTarget, 0, 10));
        Assert(new StandaloneInstaller(delegate { }).Apply(successPlan) == 0, "healthy first launch confirms update");
        Assert(UpdatePlan.Load(successPlan.PlanPath).Phase == UpdatePhase.Confirmed, "successful update phase confirmed");
        Assert(File.Exists(Path.Combine(successTarget, "PeerChat.Desktop.exe")), "replacement activated");

        string failure = Path.Combine(root, "apply-failure"); Directory.CreateDirectory(failure);
        string failureTarget = Path.Combine(failure, "Client"), failureStage = Path.Combine(failure, "stage");
        Directory.CreateDirectory(failureTarget); Directory.CreateDirectory(failureStage);
        File.Copy(healthProbe, Path.Combine(failureTarget, "PeerChat.Desktop.exe")); File.Copy(updaterCore, Path.Combine(failureTarget, "PeerChat.Updater.Core.dll")); File.WriteAllText(Path.Combine(failureTarget, "old.marker"), "old");
        File.Copy(healthProbe, Path.Combine(failureStage, "PeerChat.Desktop.exe")); File.Copy(updaterCore, Path.Combine(failureStage, "PeerChat.Updater.Core.dll")); File.WriteAllText(Path.Combine(failureStage, "fail-health"), "fail");
        PreparedUpdate failing = new PreparedUpdate { Manifest = new UpdateManifest { Version = "4.0.1-dev.1", EntryExecutable = "PeerChat.Desktop.exe" }, StagedDirectory = failureStage };
        UpdatePlan failurePlan = UpdatePlan.Load(UpdatePlan.CreateAndSave(failing, failureTarget, 0, 10));
        Assert(new StandaloneInstaller(delegate { }).Apply(failurePlan) == 2, "missing first-launch confirmation triggers rollback");
        Assert(File.Exists(Path.Combine(failureTarget, "old.marker")), "failed update restores prior installation");
        Assert(!File.Exists(Path.Combine(failureTarget, "fail-health")), "failed replacement is removed from active target");
        Assert(UpdatePlan.Load(failurePlan.PlanPath).Phase == UpdatePhase.RolledBack, "failed first launch journals rollback");
    }

    static void TestTransientClientRenameRetry(string root, string healthProbe)
    {
        string updaterCore = Path.Combine(Path.GetDirectoryName(healthProbe), "PeerChat.Updater.Core.dll");
        string scenario = Path.Combine(root, "rename-retry"); string target = Path.Combine(scenario, "Client"), stage = Path.Combine(scenario, "stage");
        Directory.CreateDirectory(target); Directory.CreateDirectory(stage);
        File.WriteAllText(Path.Combine(target, "old.marker"), "old");
        File.Copy(healthProbe, Path.Combine(stage, "PeerChat.Desktop.exe"));
        File.Copy(updaterCore, Path.Combine(stage, "PeerChat.Updater.Core.dll"));
        PreparedUpdate prepared = new PreparedUpdate { Manifest = new UpdateManifest { Version = "4.0.1-dev.1", EntryExecutable = "PeerChat.Desktop.exe" }, StagedDirectory = stage };
        UpdatePlan plan = UpdatePlan.Load(UpdatePlan.CreateAndSave(prepared, target, 0, 10));
        int targetRenameAttempts = 0; List<string> log = new List<string>();
        Action<string, string> move = (source, destination) => {
            if (String.Equals(source, plan.TargetDirectory, StringComparison.OrdinalIgnoreCase))
            {
                ++targetRenameAttempts;
                if (targetRenameAttempts == 1) throw new IOException("Simulated transient sharing lock.", unchecked((int)0x80070020));
            }
            Directory.Move(source, destination);
        };
        Assert(new StandaloneInstaller(message => log.Add(message), move).Apply(plan) == 0, "transient Client rename lock retries and applies update");
        Assert(targetRenameAttempts == 2, "Client rename retried exactly once after transient sharing lock");
        Assert(log.Exists(message => message.IndexOf("retrying in", StringComparison.OrdinalIgnoreCase) >= 0), "rename retry is logged with delay");
        Assert(UpdatePlan.Load(plan.PlanPath).Phase == UpdatePhase.Confirmed, "rename retry transaction confirms healthy replacement");
    }

    static void TestExternalLauncherWorkingDirectory(string root)
    {
        string client = Path.Combine(root, "Client");
        string launcher = Path.Combine(root, "launcher-physical-test", "PeerChat.Updater.exe");
        ProcessStartInfo start = PeerChatUpdateService.CreateStandaloneLauncherStartInfo(launcher, Path.Combine(root, "plan.json"));
        Assert(Path.GetFullPath(start.WorkingDirectory).TrimEnd(Path.DirectorySeparatorChar) ==
               Path.GetDirectoryName(Path.GetFullPath(launcher)).TrimEnd(Path.DirectorySeparatorChar),
               "external updater uses its launcher directory as working directory");
        Assert(!Path.GetFullPath(start.WorkingDirectory).StartsWith(Path.GetFullPath(client), StringComparison.OrdinalIgnoreCase),
               "external updater working directory is outside Client");
        Assert(!String.IsNullOrWhiteSpace(start.WorkingDirectory), "updater launch never inherits an unspecified working directory");
    }

    public static int Main(string[] args)
    {
        string root = Path.Combine(Path.GetTempPath(), "PeerChat-UpdaterTests-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(root);
        try
        {
            TestSemanticVersions(); TestManifestParsing(root); TestHashVerification(root); TestReleaseSignatures(root);
            TestInterruptedDownload(root); TestDowngradeRejection(root); TestRollbackRecovery(root);
            Assert(args.Length == 1 && File.Exists(args[0]), "health probe supplied by test build");
            TestStandaloneApplyAndFirstLaunchRollback(root, Path.GetFullPath(args[0]));
            TestTransientClientRenameRetry(root, Path.GetFullPath(args[0]));
            TestExternalLauncherWorkingDirectory(root);
            Console.WriteLine("PeerChat updater tests passed (" + assertions + " assertions)."); return 0;
        }
        finally { try { Directory.Delete(root, true); } catch { } }
    }

    sealed class InterruptingTransport : IUpdateTransport
    {
        readonly string manifest; readonly byte[] package; bool interrupted;
        public InterruptingTransport(string manifest, byte[] package) { this.manifest = manifest; this.package = package; }
        public Task<string> GetManifestAsync(Uri uri, CancellationToken token) { return Task.FromResult(manifest); }
        public Task DownloadAsync(Uri uri, string partialPath, long expectedSize, long maximumBytes, CancellationToken token)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(partialPath));
            if (!interrupted)
            {
                interrupted = true;
                byte[] partial = new byte[package.Length / 2]; Buffer.BlockCopy(package, 0, partial, 0, partial.Length);
                File.WriteAllBytes(partialPath, partial);
                throw new IOException("Simulated network interruption.");
            }
            File.WriteAllBytes(partialPath, package); return Task.FromResult(0);
        }
    }
}
