using System.IO;
using System.Text.Json;
using System.Windows;
using Microsoft.Web.WebView2.Core;

namespace PrismForge.Control;

public partial class MainWindow : Window
{
    private readonly PrismForgePipeClient _pipeClient = new();
    private readonly Queue<string> _pendingWebMessages = new();
    private readonly CancellationTokenSource _lifetime = new();
    private bool _webReady;

    public MainWindow()
    {
        InitializeComponent();
        Loaded += OnLoaded;
        Closed += OnClosed;
        _pipeClient.EnvelopeReceived += OnEnvelopeReceived;
        _pipeClient.StatusChanged += OnPipeStatusChanged;
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        try
        {
            await InitializeWebViewAsync();
            _pipeClient.Start();
        }
        catch (Exception exception)
        {
            LoadingText.Text = $"Control surface failed to start: {exception.Message}";
        }
    }

    private async Task InitializeWebViewAsync()
    {
        var userData = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "PrismForge",
            "ControlWebView2");
        Directory.CreateDirectory(userData);

        var environment = await CoreWebView2Environment.CreateAsync(userDataFolder: userData);
        await Browser.EnsureCoreWebView2Async(environment);

        var assetsPath = Path.Combine(AppContext.BaseDirectory, "wwwroot");
        var indexPath = Path.Combine(assetsPath, "index.html");
        if (!File.Exists(indexPath))
        {
            throw new FileNotFoundException("Bundled control assets are missing.", indexPath);
        }

        Browser.CoreWebView2.Settings.AreDefaultContextMenusEnabled = false;
        Browser.CoreWebView2.Settings.AreBrowserAcceleratorKeysEnabled = false;
#if DEBUG
        Browser.CoreWebView2.Settings.AreDevToolsEnabled = true;
#else
        Browser.CoreWebView2.Settings.AreDevToolsEnabled = false;
#endif
        Browser.CoreWebView2.SetVirtualHostNameToFolderMapping(
            WebOriginPolicy.VirtualHost,
            assetsPath,
            CoreWebView2HostResourceAccessKind.DenyCors);
        Browser.CoreWebView2.NavigationStarting += OnNavigationStarting;
        Browser.CoreWebView2.NewWindowRequested += OnNewWindowRequested;
        Browser.CoreWebView2.WebMessageReceived += OnWebMessageReceived;
        Browser.CoreWebView2.NavigationCompleted += OnNavigationCompleted;
        Browser.Source = new Uri($"https://{WebOriginPolicy.VirtualHost}/index.html");
    }

    private void OnNavigationStarting(object? sender, CoreWebView2NavigationStartingEventArgs e)
    {
        if (WebOriginPolicy.IsAllowed(e.Uri))
        {
            return;
        }

        e.Cancel = true;
        LoadingText.Text = "Blocked navigation outside the local control surface.";
    }

    private static void OnNewWindowRequested(object? sender, CoreWebView2NewWindowRequestedEventArgs e)
    {
        e.Handled = true;
    }

    private void OnNavigationCompleted(object? sender, CoreWebView2NavigationCompletedEventArgs e)
    {
        if (!e.IsSuccess)
        {
            LoadingText.Text = $"Control surface navigation failed: {e.WebErrorStatus}";
            return;
        }

        _webReady = true;
        LoadingLayer.Visibility = Visibility.Collapsed;
        while (_pendingWebMessages.TryDequeue(out var json))
        {
            Browser.CoreWebView2.PostWebMessageAsJson(json);
        }
    }

    private async void OnWebMessageReceived(object? sender, CoreWebView2WebMessageReceivedEventArgs e)
    {
        try
        {
            if (!WebOriginPolicy.IsAllowed(e.Source))
            {
                throw new ProtocolException("Rejected a web message outside the local control origin.");
            }

            using var document = JsonDocument.Parse(e.WebMessageAsJson);
            var root = document.RootElement;
            if (!root.TryGetProperty("kind", out var kind) || kind.GetString() != "EngineEnvelope" ||
                !root.TryGetProperty("envelope", out var rawEnvelope))
            {
                throw new ProtocolException("Web message must contain an EngineEnvelope.");
            }

            var envelope = ProtocolCodec.Decode(rawEnvelope.GetRawText());
            if (envelope.Type != "Command")
            {
                throw new ProtocolException("The control surface may only send Command envelopes.");
            }

            await _pipeClient.SendAsync(envelope, _lifetime.Token);
        }
        catch (Exception exception) when (exception is JsonException or ProtocolException or IOException)
        {
            await PostHostStatusAsync("error", 0, exception.Message);
        }
    }

    private void OnEnvelopeReceived(string json)
    {
        _ = Dispatcher.InvokeAsync(() => PostWebJson(json));
    }

    private void OnPipeStatusChanged(PipeStatus status)
    {
        var state = status.State switch
        {
            PipeConnectionState.Connected => "connected",
            PipeConnectionState.Connecting => "connecting",
            _ => "disconnected"
        };
        _ = PostHostStatusAsync(state, status.Attempt, status.Message);
    }

    private Task PostHostStatusAsync(string status, int attempt, string message)
    {
        return Dispatcher.InvokeAsync(() =>
        {
            var json = JsonSerializer.Serialize(new
            {
                kind = "HostStatus",
                payload = new { status, attempt, message }
            });
            PostWebJson(json);
        }).Task;
    }

    private void PostWebJson(string json)
    {
        if (!_webReady || Browser.CoreWebView2 is null)
        {
            _pendingWebMessages.Enqueue(json);
            while (_pendingWebMessages.Count > 128)
            {
                _pendingWebMessages.Dequeue();
            }
            return;
        }

        Browser.CoreWebView2.PostWebMessageAsJson(json);
    }

    private async void OnClosed(object? sender, EventArgs e)
    {
        _lifetime.Cancel();
        if (Browser.CoreWebView2 is not null)
        {
            Browser.CoreWebView2.NavigationStarting -= OnNavigationStarting;
            Browser.CoreWebView2.NewWindowRequested -= OnNewWindowRequested;
            Browser.CoreWebView2.WebMessageReceived -= OnWebMessageReceived;
            Browser.CoreWebView2.NavigationCompleted -= OnNavigationCompleted;
        }
        _pipeClient.EnvelopeReceived -= OnEnvelopeReceived;
        _pipeClient.StatusChanged -= OnPipeStatusChanged;
        await _pipeClient.DisposeAsync();
        _lifetime.Dispose();
    }
}
