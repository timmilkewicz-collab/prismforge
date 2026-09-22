using System.Windows;

namespace PrismForge.Control;

public partial class App : Application
{
    private const string MutexName = @"Local\PrismForge.Control";
    private const string ActivationEventName = @"Local\PrismForge.Control.Activate";
    private Mutex? _singleInstanceMutex;
    private EventWaitHandle? _activationEvent;
    private CancellationTokenSource? _activationLifetime;

    protected override void OnStartup(StartupEventArgs e)
    {
        _singleInstanceMutex = new Mutex(true, MutexName, out var isFirstInstance);
        if (!isFirstInstance)
        {
            SignalExistingInstance();
            _singleInstanceMutex.Dispose();
            _singleInstanceMutex = null;
            Shutdown();
            return;
        }

        base.OnStartup(e);
        _activationLifetime = new CancellationTokenSource();
        _activationEvent = new EventWaitHandle(false, EventResetMode.AutoReset, ActivationEventName);
        _ = Task.Run(() => ListenForActivation(_activationLifetime.Token));

        var window = new MainWindow();
        MainWindow = window;
        window.Show();
    }

    private static void SignalExistingInstance()
    {
        for (var attempt = 0; attempt < 10; attempt++)
        {
            try
            {
                using var activationEvent = EventWaitHandle.OpenExisting(ActivationEventName);
                activationEvent.Set();
                return;
            }
            catch (WaitHandleCannotBeOpenedException)
            {
                Thread.Sleep(50);
            }
        }
    }

    private void ListenForActivation(CancellationToken cancellationToken)
    {
        if (_activationEvent is null)
        {
            return;
        }

        var handles = new[] { _activationEvent, cancellationToken.WaitHandle };
        while (!cancellationToken.IsCancellationRequested)
        {
            var selected = WaitHandle.WaitAny(handles);
            if (selected != 0)
            {
                return;
            }

            _ = Dispatcher.InvokeAsync(BringMainWindowForward);
        }
    }

    private void BringMainWindowForward()
    {
        if (MainWindow is null)
        {
            return;
        }

        if (MainWindow.WindowState == WindowState.Minimized)
        {
            MainWindow.WindowState = WindowState.Normal;
        }

        MainWindow.Show();
        MainWindow.Activate();
        MainWindow.Topmost = true;
        MainWindow.Topmost = false;
        MainWindow.Focus();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _activationLifetime?.Cancel();
        _activationEvent?.Set();
        _activationEvent?.Dispose();
        _activationLifetime?.Dispose();

        if (_singleInstanceMutex is not null)
        {
            try
            {
                _singleInstanceMutex.ReleaseMutex();
            }
            catch (ApplicationException)
            {
            }
            _singleInstanceMutex.Dispose();
        }

        base.OnExit(e);
    }
}
