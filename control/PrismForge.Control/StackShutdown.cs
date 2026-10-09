using System.Diagnostics;
using System.IO;
using System.Text.Json;
using System.Windows;

namespace PrismForge.Control;

internal static class StackShutdown
{
    public static async Task ShutdownEngineAndExitAsync(
        PrismForgePipeClient pipe,
        CancellationToken cancellationToken)
    {
        var enginePath = Path.Combine(AppContext.BaseDirectory, "PrismForge.Engine.exe");
        try
        {
            using var payload = JsonDocument.Parse("{\"action\":\"shutdownApplication\"}");
            var envelope = new ProtocolEnvelope(
                ProtocolCodec.ProtocolVersion,
                "Command",
                Guid.NewGuid().ToString("N"),
                payload.RootElement.Clone());
            await pipe.SendAsync(envelope, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception)
        {
            // Pipe may already be down; fall through to process cleanup.
        }

        for (var attempt = 0; attempt < 40; attempt++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!IsEngineRunning(enginePath))
            {
                break;
            }

            await Task.Delay(100, cancellationToken).ConfigureAwait(false);
        }

        if (IsEngineRunning(enginePath))
        {
            TerminateEngine(enginePath);
        }

        Application.Current.Shutdown();
    }

    private static bool IsEngineRunning(string enginePath)
    {
        var expected = Path.GetFullPath(enginePath);
        foreach (var process in Process.GetProcessesByName("PrismForge.Engine"))
        {
            using (process)
            {
                if (TryGetExecutablePath(process, out var image) &&
                    string.Equals(Path.GetFullPath(image), expected, StringComparison.OrdinalIgnoreCase))
                {
                    return true;
                }
            }
        }

        return false;
    }

    private static void TerminateEngine(string enginePath)
    {
        var expected = Path.GetFullPath(enginePath);
        foreach (var process in Process.GetProcessesByName("PrismForge.Engine"))
        {
            using (process)
            {
                if (!TryGetExecutablePath(process, out var image) ||
                    !string.Equals(Path.GetFullPath(image), expected, StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }

                try
                {
                    if (!process.HasExited)
                    {
                        process.Kill(entireProcessTree: true);
                        process.WaitForExit(3000);
                    }
                }
                catch (Exception)
                {
                    // Best effort only.
                }
            }
        }
    }

    private static bool TryGetExecutablePath(Process process, out string path)
    {
        path = string.Empty;
        try
        {
            path = process.MainModule?.FileName ?? string.Empty;
            return !string.IsNullOrWhiteSpace(path);
        }
        catch (Exception)
        {
            return false;
        }
    }
}
