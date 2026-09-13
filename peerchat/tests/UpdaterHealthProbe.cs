using System;
using System.IO;
using PeerChat.Updater;

static class UpdaterHealthProbe
{
    public static int Main(string[] args)
    {
        if (File.Exists(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "fail-health"))) return 1;
        return UpdateHealthReporter.MarkHealthyFromCommandLine(args) ? 0 : 0;
    }
}
