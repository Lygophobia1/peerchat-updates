using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Web.Script.Serialization;

namespace PeerChat.Updater
{
    public sealed class SemanticVersion : IComparable<SemanticVersion>
    {
        public int Major { get; private set; }
        public int Minor { get; private set; }
        public int Patch { get; private set; }
        public string PreRelease { get; private set; }
        public string BuildMetadata { get; private set; }

        private SemanticVersion() { }

        public static SemanticVersion Parse(string value)
        {
            SemanticVersion parsed;
            string error;
            if (!TryParse(value, out parsed, out error))
                throw new FormatException(error);
            return parsed;
        }

        public static bool TryParse(string value, out SemanticVersion parsed, out string error)
        {
            parsed = null;
            error = null;
            if (String.IsNullOrWhiteSpace(value)) { error = "Version is empty."; return false; }
            string input = value.Trim();
            if (input.StartsWith("v", StringComparison.OrdinalIgnoreCase)) input = input.Substring(1);
            string build = "";
            int plus = input.IndexOf('+');
            if (plus >= 0) { build = input.Substring(plus + 1); input = input.Substring(0, plus); }
            string pre = "";
            int dash = input.IndexOf('-');
            if (dash >= 0) { pre = input.Substring(dash + 1); input = input.Substring(0, dash); }
            string[] core = input.Split('.');
            int major, minor, patch;
            if (core.Length != 3 || !ParseCore(core[0], out major) || !ParseCore(core[1], out minor) || !ParseCore(core[2], out patch))
            { error = "Version must contain non-negative major.minor.patch numbers without leading zeros."; return false; }
            if (!ValidIdentifiers(pre, true) || !ValidIdentifiers(build, false))
            { error = "Pre-release or build metadata contains an invalid identifier."; return false; }
            parsed = new SemanticVersion { Major = major, Minor = minor, Patch = patch, PreRelease = pre, BuildMetadata = build };
            return true;
        }

        static bool ParseCore(string value, out int number)
        {
            number = 0;
            return value.Length > 0 && (value.Length == 1 || value[0] != '0') &&
                   Int32.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out number) && number >= 0;
        }

        static bool ValidIdentifiers(string value, bool enforceNumericLeadingZeros)
        {
            if (value.Length == 0) return true;
            foreach (string part in value.Split('.'))
            {
                if (part.Length == 0) return false;
                bool numeric = true;
                foreach (char c in part)
                {
                    if (!(Char.IsLetterOrDigit(c) || c == '-')) return false;
                    if (!Char.IsDigit(c)) numeric = false;
                }
                if (enforceNumericLeadingZeros && numeric && part.Length > 1 && part[0] == '0') return false;
            }
            return true;
        }

        public int CompareTo(SemanticVersion other)
        {
            if (other == null) return 1;
            int core = Major.CompareTo(other.Major);
            if (core == 0) core = Minor.CompareTo(other.Minor);
            if (core == 0) core = Patch.CompareTo(other.Patch);
            if (core != 0) return core;
            if (PreRelease.Length == 0) return other.PreRelease.Length == 0 ? 0 : 1;
            if (other.PreRelease.Length == 0) return -1;
            string[] left = PreRelease.Split('.'), right = other.PreRelease.Split('.');
            int length = Math.Max(left.Length, right.Length);
            for (int i = 0; i < length; ++i)
            {
                if (i >= left.Length) return -1;
                if (i >= right.Length) return 1;
                bool lNumeric = IsNumericIdentifier(left[i]);
                bool rNumeric = IsNumericIdentifier(right[i]);
                int part;
                if (lNumeric && rNumeric)
                {
                    part = left[i].Length.CompareTo(right[i].Length);
                    if (part == 0) part = StringComparer.Ordinal.Compare(left[i], right[i]);
                }
                else if (lNumeric != rNumeric) part = lNumeric ? -1 : 1;
                else part = StringComparer.Ordinal.Compare(left[i], right[i]);
                if (part != 0) return part;
            }
            return 0;
        }

        static bool IsNumericIdentifier(string value)
        {
            if (value.Length == 0) return false;
            foreach (char c in value) if (!Char.IsDigit(c)) return false;
            return true;
        }

        public override string ToString()
        {
            return Major + "." + Minor + "." + Patch +
                   (PreRelease.Length == 0 ? "" : "-" + PreRelease) +
                   (BuildMetadata.Length == 0 ? "" : "+" + BuildMetadata);
        }
    }

    public sealed class UpdateManifest
    {
        public int SchemaVersion { get; set; }
        public string Product { get; set; }
        public string Channel { get; set; }
        public string Version { get; set; }
        public string MinimumCurrentVersion { get; set; }
        public string PackageUrl { get; set; }
        public string PackageSha256 { get; set; }
        public long PackageSize { get; set; }
        public string EntryExecutable { get; set; }
        public string ReleaseNotesUrl { get; set; }
        public string PublishedUtc { get; set; }
        public UpdateSignature Signature { get; set; }

        public void Validate(UpdateFeedConfiguration configuration)
        {
            if (SchemaVersion != 1) throw new InvalidDataException("Unsupported update manifest schema version.");
            if (!String.Equals(Product, "PeerChat", StringComparison.Ordinal)) throw new InvalidDataException("Manifest product is not PeerChat.");
            if (!String.Equals(Channel, configuration.Channel, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Manifest channel does not match updater configuration.");
            SemanticVersion.Parse(Version);
            if (!String.IsNullOrWhiteSpace(MinimumCurrentVersion)) SemanticVersion.Parse(MinimumCurrentVersion);
            Uri package;
            if (!Uri.TryCreate(PackageUrl, UriKind.Absolute, out package) ||
                (!configuration.AllowInsecureDevelopmentUrls && package.Scheme != Uri.UriSchemeHttps))
                throw new InvalidDataException("Package URL must be an absolute HTTPS URL.");
            if (String.IsNullOrWhiteSpace(PackageSha256) || PackageSha256.Length != 64 || !IsHex(PackageSha256))
                throw new InvalidDataException("Package SHA-256 must contain exactly 64 hexadecimal characters.");
            if (PackageSize <= 0 || PackageSize > configuration.MaximumPackageBytes)
                throw new InvalidDataException("Package size is outside the configured safety limit.");
            ValidateRelativePath(EntryExecutable, "Entry executable");
            DateTime published;
            if (!DateTime.TryParse(PublishedUtc, CultureInfo.InvariantCulture, DateTimeStyles.AdjustToUniversal | DateTimeStyles.AssumeUniversal, out published))
                throw new InvalidDataException("PublishedUtc is invalid.");
            if (Signature != null && (String.IsNullOrWhiteSpace(Signature.Algorithm) || String.IsNullOrWhiteSpace(Signature.KeyId) || String.IsNullOrWhiteSpace(Signature.Value)))
                throw new InvalidDataException("Signature metadata is incomplete.");
        }

        public byte[] GetSignaturePayload()
        {
            // A fixed field order gives a future signature verifier one stable payload independent of JSON formatting.
            string payload = "PeerChatUpdateManifest\n1\n" + Product + "\n" + Channel + "\n" + Version + "\n" +
                             (MinimumCurrentVersion ?? "") + "\n" + PackageUrl + "\n" + PackageSha256.ToLowerInvariant() + "\n" +
                             PackageSize.ToString(CultureInfo.InvariantCulture) + "\n" + EntryExecutable + "\n" +
                             (ReleaseNotesUrl ?? "") + "\n" + PublishedUtc + "\n";
            return Encoding.UTF8.GetBytes(payload);
        }

        internal static void ValidateRelativePath(string path, string label)
        {
            if (String.IsNullOrWhiteSpace(path) || Path.IsPathRooted(path) || path.IndexOf(':') >= 0)
                throw new InvalidDataException(label + " must be a relative path.");
            string normalized = path.Replace('/', '\\');
            foreach (string part in normalized.Split('\\'))
                if (part == ".." || part == "." || part.Length == 0) throw new InvalidDataException(label + " contains an unsafe path segment.");
        }

        static bool IsHex(string value)
        {
            foreach (char c in value)
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
            return true;
        }
    }

    public sealed class UpdateSignature
    {
        public string Algorithm { get; set; }
        public string KeyId { get; set; }
        public string Value { get; set; }
    }

    public sealed class UpdateFeedConfiguration
    {
        public string ManifestUrl { get; set; }
        public string Channel { get; set; }
        public string StagingRoot { get; set; }
        public bool RequireSignature { get; set; }
        public bool AllowInsecureDevelopmentUrls { get; set; }
        public long MaximumPackageBytes { get; set; }
        public long MaximumExtractedBytes { get; set; }
        public int MaximumArchiveFiles { get; set; }
        public int FirstLaunchTimeoutSeconds { get; set; }

        public static UpdateFeedConfiguration CreateDefaults()
        {
            return new UpdateFeedConfiguration {
                Channel = "release",
                StagingRoot = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PeerChat", "Updates"),
                RequireSignature = true,
                MaximumPackageBytes = 512L * 1024L * 1024L,
                MaximumExtractedBytes = 1024L * 1024L * 1024L,
                MaximumArchiveFiles = 4096,
                FirstLaunchTimeoutSeconds = 30
            };
        }

        public static UpdateFeedConfiguration Load(string path)
        {
            if (String.IsNullOrWhiteSpace(path) || !File.Exists(path)) throw new FileNotFoundException("Updater configuration was not found.", path);
            UpdateFeedConfiguration value = new JavaScriptSerializer().Deserialize<UpdateFeedConfiguration>(File.ReadAllText(path));
            if (value == null) throw new InvalidDataException("Updater configuration is empty.");
            value.Validate();
            return value;
        }

        public void Validate()
        {
            StagingRoot = Environment.ExpandEnvironmentVariables(StagingRoot ?? "");
            if (String.IsNullOrWhiteSpace(Channel)) throw new InvalidDataException("Update channel is required.");
            Uri manifest;
            if (!Uri.TryCreate(ManifestUrl, UriKind.Absolute, out manifest) ||
                (!AllowInsecureDevelopmentUrls && manifest.Scheme != Uri.UriSchemeHttps))
                throw new InvalidDataException("Manifest URL must be an absolute HTTPS URL.");
            if (String.IsNullOrWhiteSpace(StagingRoot)) throw new InvalidDataException("Staging root is required.");
            if (MaximumPackageBytes <= 0 || MaximumExtractedBytes <= 0 || MaximumArchiveFiles <= 0)
                throw new InvalidDataException("Updater safety limits must be positive.");
            if (FirstLaunchTimeoutSeconds < 5 || FirstLaunchTimeoutSeconds > 300)
                throw new InvalidDataException("First-launch timeout must be between 5 and 300 seconds.");
        }
    }

    public static class UpdateJson
    {
        static readonly JavaScriptSerializer Serializer = new JavaScriptSerializer { MaxJsonLength = 256 * 1024 };
        public static UpdateManifest ParseManifest(string json, UpdateFeedConfiguration configuration)
        {
            if (String.IsNullOrWhiteSpace(json) || Encoding.UTF8.GetByteCount(json) > 256 * 1024)
                throw new InvalidDataException("Update manifest is empty or too large.");
            UpdateManifest manifest;
            try { manifest = Serializer.Deserialize<UpdateManifest>(json); }
            catch (Exception error) { throw new InvalidDataException("Update manifest is malformed.", error); }
            if (manifest == null) throw new InvalidDataException("Update manifest is empty.");
            manifest.Validate(configuration);
            return manifest;
        }

        public static string Serialize(object value) { return Serializer.Serialize(value); }
        public static T DeserializeFile<T>(string path) { return Serializer.Deserialize<T>(File.ReadAllText(path)); }
    }
}
