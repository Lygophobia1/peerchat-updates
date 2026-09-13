using System;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace PeerChat.Updater
{
    public interface IUpdateTransport
    {
        Task<string> GetManifestAsync(Uri uri, CancellationToken cancellationToken);
        Task DownloadAsync(Uri uri, string partialPath, long expectedSize, long maximumBytes, CancellationToken cancellationToken);
    }

    public sealed class HttpUpdateTransport : IUpdateTransport, IDisposable
    {
        readonly HttpClient client;
        readonly bool allowInsecureDevelopmentUrls;
        public HttpUpdateTransport(bool allowInsecureDevelopmentUrls = false)
        {
            this.allowInsecureDevelopmentUrls = allowInsecureDevelopmentUrls;
            ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
            HttpClientHandler handler = new HttpClientHandler { AllowAutoRedirect = true, CheckCertificateRevocationList = true };
            client = new HttpClient(handler) { Timeout = TimeSpan.FromMinutes(15) };
        }

        public async Task<string> GetManifestAsync(Uri uri, CancellationToken cancellationToken)
        {
            using (HttpResponseMessage response = await client.GetAsync(uri, HttpCompletionOption.ResponseHeadersRead, cancellationToken).ConfigureAwait(false))
            {
                EnsureSuccessfulHttpsResponse(response);
                long? size = response.Content.Headers.ContentLength;
                if (size.HasValue && size.Value > 256 * 1024) throw new InvalidDataException("Update manifest exceeds 256 KiB.");
                using (Stream stream = await response.Content.ReadAsStreamAsync().ConfigureAwait(false))
                using (MemoryStream buffer = new MemoryStream())
                {
                    await CopyBoundedAsync(stream, buffer, 256 * 1024, cancellationToken).ConfigureAwait(false);
                    return Encoding.UTF8.GetString(buffer.ToArray());
                }
            }
        }

        public async Task DownloadAsync(Uri uri, string partialPath, long expectedSize, long maximumBytes, CancellationToken cancellationToken)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(partialPath));
            long offset = File.Exists(partialPath) ? new FileInfo(partialPath).Length : 0;
            if (offset > expectedSize || offset > maximumBytes) { File.Delete(partialPath); offset = 0; }
            using (HttpRequestMessage request = new HttpRequestMessage(HttpMethod.Get, uri))
            {
                if (offset > 0) request.Headers.Range = new RangeHeaderValue(offset, null);
                using (HttpResponseMessage response = await client.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, cancellationToken).ConfigureAwait(false))
                {
                    EnsureSuccessfulHttpsResponse(response);
                    bool append = offset > 0 && response.StatusCode == HttpStatusCode.PartialContent;
                    if (offset > 0 && !append) offset = 0; // Server ignored Range; safely restart the download.
                    FileMode mode = append ? FileMode.Append : FileMode.Create;
                    using (Stream input = await response.Content.ReadAsStreamAsync().ConfigureAwait(false))
                    using (FileStream output = new FileStream(partialPath, mode, FileAccess.Write, FileShare.None, 81920, true))
                        await CopyBoundedAsync(input, output, maximumBytes - offset, cancellationToken).ConfigureAwait(false);
                }
            }
            long finalSize = new FileInfo(partialPath).Length;
            if (finalSize != expectedSize) throw new IOException("Downloaded package size does not match the manifest. The partial file was retained for a safe retry.");
        }

        static async Task CopyBoundedAsync(Stream input, Stream output, long maximumBytes, CancellationToken token)
        {
            byte[] buffer = new byte[81920]; long total = 0;
            while (true)
            {
                int read = await input.ReadAsync(buffer, 0, buffer.Length, token).ConfigureAwait(false);
                if (read == 0) break;
                total += read;
                if (total > maximumBytes) throw new InvalidDataException("Download exceeded its configured size limit.");
                await output.WriteAsync(buffer, 0, read, token).ConfigureAwait(false);
            }
            await output.FlushAsync(token).ConfigureAwait(false);
        }

        void EnsureSuccessfulHttpsResponse(HttpResponseMessage response)
        {
            response.EnsureSuccessStatusCode();
            Uri finalUri = response.RequestMessage == null ? null : response.RequestMessage.RequestUri;
            if (finalUri == null || (!allowInsecureDevelopmentUrls && finalUri.Scheme != Uri.UriSchemeHttps))
                throw new InvalidDataException("Update download redirected to a non-HTTPS address.");
        }

        public void Dispose() { client.Dispose(); }
    }
}
