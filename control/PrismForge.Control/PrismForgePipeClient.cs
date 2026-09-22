using System.IO;
using System.IO.Pipes;

namespace PrismForge.Control;

public enum PipeConnectionState
{
    Disconnected,
    Connecting,
    Connected
}

public sealed record PipeStatus(PipeConnectionState State, int Attempt, string Message);

public sealed class PrismForgePipeClient : IAsyncDisposable
{
    public const string PipeName = "PrismForge.v1";

    private readonly CancellationTokenSource _lifetime = new();
    private readonly SemaphoreSlim _writeGate = new(1, 1);
    private readonly object _streamGate = new();
    private NamedPipeClientStream? _stream;
    private Task? _runTask;

    public event Action<string>? EnvelopeReceived;
    public event Action<PipeStatus>? StatusChanged;

    public void Start()
    {
        _runTask ??= Task.Run(() => RunAsync(_lifetime.Token));
    }

    public async Task SendAsync(ProtocolEnvelope envelope, CancellationToken cancellationToken = default)
    {
        var frame = ProtocolCodec.Encode(envelope);
        await _writeGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            NamedPipeClientStream? stream;
            lock (_streamGate)
            {
                stream = _stream;
            }

            if (stream is null || !stream.IsConnected)
            {
                throw new IOException("PrismForge engine is not connected.");
            }

            await stream.WriteAsync(frame, cancellationToken).ConfigureAwait(false);
            await stream.FlushAsync(cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            _writeGate.Release();
        }
    }

    private async Task RunAsync(CancellationToken cancellationToken)
    {
        var attempt = 0;
        while (!cancellationToken.IsCancellationRequested)
        {
            attempt++;
            StatusChanged?.Invoke(new PipeStatus(PipeConnectionState.Connecting, attempt, "Looking for PrismForge engine"));

            using var candidate = new NamedPipeClientStream(
                ".",
                PipeName,
                PipeDirection.InOut,
                PipeOptions.Asynchronous);

            try
            {
                await candidate.ConnectAsync(1000, cancellationToken).ConfigureAwait(false);
                candidate.ReadMode = PipeTransmissionMode.Byte;

                lock (_streamGate)
                {
                    _stream = candidate;
                }

                attempt = 0;
                StatusChanged?.Invoke(new PipeStatus(PipeConnectionState.Connected, 0, "Engine connected"));
                await SendSnapshotRequestAsync(candidate, cancellationToken).ConfigureAwait(false);
                await ReadLoopAsync(candidate, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                break;
            }
            catch (Exception exception) when (exception is IOException or TimeoutException or ProtocolException)
            {
                var reconnectAttempt = Math.Max(attempt, 1);
                StatusChanged?.Invoke(new PipeStatus(PipeConnectionState.Disconnected, reconnectAttempt, FriendlyMessage(exception)));
            }
            finally
            {
                lock (_streamGate)
                {
                    if (ReferenceEquals(_stream, candidate))
                    {
                        _stream = null;
                    }
                }
            }

            if (!cancellationToken.IsCancellationRequested)
            {
                var delay = Math.Min(3000, 250 * Math.Max(1, attempt));
                await Task.Delay(delay, cancellationToken).ConfigureAwait(false);
            }
        }
    }

    private static async Task SendSnapshotRequestAsync(Stream stream, CancellationToken cancellationToken)
    {
        using var payloadDocument = System.Text.Json.JsonDocument.Parse("{\"action\":\"requestSnapshot\"}");
        var envelope = new ProtocolEnvelope(
            ProtocolCodec.ProtocolVersion,
            "Command",
            Guid.NewGuid().ToString(),
            payloadDocument.RootElement.Clone());
        var frame = ProtocolCodec.Encode(envelope);
        await stream.WriteAsync(frame, cancellationToken).ConfigureAwait(false);
        await stream.FlushAsync(cancellationToken).ConfigureAwait(false);
    }

    private async Task ReadLoopAsync(Stream stream, CancellationToken cancellationToken)
    {
        var prefix = new byte[ProtocolCodec.PrefixSize];
        while (!cancellationToken.IsCancellationRequested)
        {
            await ReadExactlyAsync(stream, prefix, cancellationToken).ConfigureAwait(false);
            var length = ProtocolCodec.DecodeLength(prefix);
            var body = GC.AllocateUninitializedArray<byte>(length);
            await ReadExactlyAsync(stream, body, cancellationToken).ConfigureAwait(false);

            var json = ProtocolCodec.DecodeUtf8(body);
            ProtocolCodec.Decode(json);
            EnvelopeReceived?.Invoke(json);
        }
    }

    private static async Task ReadExactlyAsync(Stream stream, Memory<byte> buffer, CancellationToken cancellationToken)
    {
        var read = 0;
        while (read < buffer.Length)
        {
            var count = await stream.ReadAsync(buffer[read..], cancellationToken).ConfigureAwait(false);
            if (count == 0)
            {
                throw new IOException("PrismForge engine closed the pipe.");
            }

            read += count;
        }
    }

    private static string FriendlyMessage(Exception exception) => exception switch
    {
        TimeoutException => "Engine is offline; reconnecting",
        ProtocolException => $"Protocol error: {exception.Message}",
        _ => "Engine disconnected; reconnecting"
    };

    public async ValueTask DisposeAsync()
    {
        _lifetime.Cancel();
        NamedPipeClientStream? stream;
        lock (_streamGate)
        {
            stream = _stream;
            _stream = null;
        }

        if (stream is not null)
        {
            await stream.DisposeAsync().ConfigureAwait(false);
        }

        if (_runTask is not null)
        {
            try
            {
                await _runTask.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
        }

        _writeGate.Dispose();
        _lifetime.Dispose();
    }
}
