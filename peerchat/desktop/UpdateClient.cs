using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using PeerChat;
using PeerChat.Updater;

namespace PeerChatDesktop
{
    // Update channel selection is installation-wide rather than profile state. A profile switch
    // must never silently change the binaries or feed being used by another local profile.
    public sealed class UpdateChannelPreferences
    {
        public string Channel { get; set; }

        public static UpdateChannelPreferences Load(string path)
        {
            try
            {
                if (File.Exists(path))
                {
                    UpdateChannelPreferences value = new JavaScriptSerializer().Deserialize<UpdateChannelPreferences>(File.ReadAllText(path));
                    if (value != null && IsKnown(value.Channel)) return value;
                }
            }
            catch { }
            return new UpdateChannelPreferences { Channel = "release" };
        }

        public void Save(string path)
        {
            if (!IsKnown(Channel)) throw new InvalidOperationException("Unknown update channel.");
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            string temporary = path + ".tmp";
            File.WriteAllText(temporary, new JavaScriptSerializer().Serialize(this));
            if (File.Exists(path)) File.Replace(temporary, path, null); else File.Move(temporary, path);
        }

        public static bool IsKnown(string value)
        {
            return String.Equals(value, "release", StringComparison.OrdinalIgnoreCase) ||
                   String.Equals(value, "development", StringComparison.OrdinalIgnoreCase);
        }
    }

    // This packaged, non-secret feed map deliberately contains no user credentials. Production
    // URLs and trusted signature implementation remain a release-engineering responsibility.
    public sealed class PackagedUpdateFeeds
    {
        public string ReleaseManifestUrl { get; set; }
        public string DevelopmentManifestUrl { get; set; }
        public bool DevelopmentAllowsUnsignedPackages { get; set; }
        public bool DevelopmentAllowsInsecureUrls { get; set; }

        public static PackagedUpdateFeeds Load(string path)
        {
            if (!File.Exists(path)) throw new FileNotFoundException("The packaged update feed map is missing.", path);
            PackagedUpdateFeeds feeds = new JavaScriptSerializer().Deserialize<PackagedUpdateFeeds>(File.ReadAllText(path));
            if (feeds == null) throw new InvalidDataException("The packaged update feed map is empty.");
            return feeds;
        }
    }

    public sealed class DesktopUpdateClient : IDisposable
    {
        readonly string installDirectory;
        readonly string preferencesPath;
        readonly string feedsPath;
        readonly string updaterExecutable;
        public UpdateChannelPreferences Preferences { get; private set; }

        public DesktopUpdateClient()
        {
            installDirectory = Path.GetFullPath(AppDomain.CurrentDomain.BaseDirectory);
            string stateDirectory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PeerChat", "Updater");
            preferencesPath = Path.Combine(stateDirectory, "channel.json");
            feedsPath = Path.Combine(installDirectory, "update-feeds.json");
            updaterExecutable = Path.Combine(installDirectory, "PeerChat.Updater.exe");
            Preferences = UpdateChannelPreferences.Load(preferencesPath);
        }

        public string ChannelLabel { get { return String.Equals(Preferences.Channel, "development", StringComparison.OrdinalIgnoreCase) ? "Development" : "Release"; } }

        public void SelectChannel(string channel)
        {
            if (!UpdateChannelPreferences.IsKnown(channel)) throw new InvalidOperationException("Unknown update channel.");
            Preferences.Channel = channel.ToLowerInvariant();
            Preferences.Save(preferencesPath);
        }

        public async Task<UpdateCheckResult> CheckForUpdate(CancellationToken token)
        {
            using (PeerChatUpdateService service = CreateService())
                return await service.CheckForUpdate(VersionInfo.Display, token).ConfigureAwait(false);
        }

        public async Task<Process> DownloadPrepareAndLaunch(UpdateCheckResult update, int currentProcessId, CancellationToken token)
        {
            using (PeerChatUpdateService service = CreateService())
            {
                DownloadedUpdate downloaded = await service.DownloadUpdate(update, token).ConfigureAwait(false);
                PreparedUpdate prepared = service.PrepareUpdate(downloaded);
                return service.LaunchUpdaterAndExit(prepared, installDirectory, currentProcessId);
            }
        }

        PeerChatUpdateService CreateService()
        {
            if (!File.Exists(updaterExecutable)) throw new FileNotFoundException("The packaged PeerChat updater is missing.", updaterExecutable);
            PackagedUpdateFeeds feeds = PackagedUpdateFeeds.Load(feedsPath);
            bool development = String.Equals(Preferences.Channel, "development", StringComparison.OrdinalIgnoreCase);
            string manifest = development ? feeds.DevelopmentManifestUrl : feeds.ReleaseManifestUrl;
            if (String.IsNullOrWhiteSpace(manifest) || manifest.IndexOf("updates.peerchat.example", StringComparison.OrdinalIgnoreCase) >= 0)
                throw new InvalidOperationException("The " + ChannelLabel + " update feed has not been configured for this package.");
            UpdateFeedConfiguration configuration = UpdateFeedConfiguration.CreateDefaults();
            configuration.Channel = development ? "development" : "release";
            configuration.ManifestUrl = manifest;
            configuration.RequireSignature = !development || !feeds.DevelopmentAllowsUnsignedPackages;
            configuration.AllowInsecureDevelopmentUrls = development && feeds.DevelopmentAllowsInsecureUrls;
            configuration.Validate();
            return new PeerChatUpdateService(configuration, new HttpUpdateTransport(configuration.AllowInsecureDevelopmentUrls),
                new SignatureBoundaryVerifier(configuration.RequireSignature), PrepareStandaloneUpdater(configuration.StagingRoot));
        }

        string PrepareStandaloneUpdater(string stagingRoot)
        {
            // The updater cannot remain loaded from the installation directory it will rename.
            // Copy only its explicit runtime trio to a unique, install-wide staging directory.
            string sourceCore = Path.Combine(installDirectory, "PeerChat.Updater.Core.dll");
            string sourceConfig = Path.Combine(installDirectory, "PeerChat.Updater.exe.config");
            if (!File.Exists(sourceCore) || !File.Exists(sourceConfig)) throw new FileNotFoundException("The packaged PeerChat updater runtime is incomplete.");
            string launcher = Path.Combine(stagingRoot, "launcher-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(launcher);
            string targetExe = Path.Combine(launcher, "PeerChat.Updater.exe");
            File.Copy(updaterExecutable, targetExe, true);
            File.Copy(sourceCore, Path.Combine(launcher, "PeerChat.Updater.Core.dll"), true);
            File.Copy(sourceConfig, Path.Combine(launcher, "PeerChat.Updater.exe.config"), true);
            return targetExe;
        }

        public void Dispose() { }
    }
}
