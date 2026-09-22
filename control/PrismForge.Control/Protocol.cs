using System.Buffers.Binary;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace PrismForge.Control;

public sealed record ProtocolEnvelope(
    [property: JsonPropertyName("version")] int Version,
    [property: JsonPropertyName("type")] string Type,
    [property: JsonPropertyName("requestId")] string? RequestId,
    [property: JsonPropertyName("payload")] JsonElement Payload);

public static class ProtocolCodec
{
    public const int ProtocolVersion = 1;
    public const int PrefixSize = 4;
    // Match the Engine's v1 pipe cap in both directions.
    public const int MaxMessageBytes = 65_536;

    private static readonly HashSet<string> EnvelopeTypes = new(StringComparer.Ordinal)
    {
        "Command",
        "StateSnapshot",
        "SignalFrame",
        "DeviceEvent",
        "ErrorEvent"
    };

    private static readonly HashSet<string> CommandActions = new(StringComparer.Ordinal)
    {
        "requestSnapshot",
        "setScene",
        "setCrossfader",
        "setBlackout",
        "setPanicDim",
        "setEffect",
        "setModulation",
        "saveCue",
        "recallCue",
        "setAudioSource",
        "saveShow",
        "loadShow"
    };

    private static readonly JsonSerializerOptions SerializerOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        PropertyNameCaseInsensitive = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
    };

    public static ProtocolEnvelope Decode(string json)
    {
        if (string.IsNullOrWhiteSpace(json))
        {
            throw new ProtocolException("The envelope is empty.");
        }

        ProtocolEnvelope? envelope;
        try
        {
            envelope = JsonSerializer.Deserialize<ProtocolEnvelope>(json, SerializerOptions);
        }
        catch (JsonException exception)
        {
            throw new ProtocolException("The envelope is not valid JSON.", exception);
        }

        if (envelope is null)
        {
            throw new ProtocolException("The envelope is empty.");
        }

        ValidateEnvelope(envelope);
        return envelope;
    }

    public static byte[] Encode(ProtocolEnvelope envelope)
    {
        ValidateEnvelope(envelope);
        var body = JsonSerializer.SerializeToUtf8Bytes(envelope, SerializerOptions);
        if (body.Length == 0 || body.Length > MaxMessageBytes)
        {
            throw new ProtocolException($"Envelope length {body.Length} is outside the permitted range.");
        }

        var framed = new byte[PrefixSize + body.Length];
        BinaryPrimitives.WriteUInt32LittleEndian(framed.AsSpan(0, PrefixSize), (uint)body.Length);
        body.CopyTo(framed.AsSpan(PrefixSize));
        return framed;
    }

    public static string Serialize(ProtocolEnvelope envelope)
    {
        ValidateEnvelope(envelope);
        return JsonSerializer.Serialize(envelope, SerializerOptions);
    }

    public static void ValidateEnvelope(ProtocolEnvelope envelope)
    {
        if (envelope.Version != ProtocolVersion)
        {
            throw new ProtocolException($"Unsupported protocol version {envelope.Version}.");
        }

        if (!EnvelopeTypes.Contains(envelope.Type))
        {
            throw new ProtocolException($"Unknown envelope type '{envelope.Type}'.");
        }

        if (envelope.Payload.ValueKind != JsonValueKind.Object)
        {
            throw new ProtocolException("Envelope payload must be a JSON object.");
        }

        if (envelope.Type == "Command")
        {
            if (string.IsNullOrWhiteSpace(envelope.RequestId))
            {
                throw new ProtocolException("Command requestId is required.");
            }

            ValidateCommand(envelope.Payload);
        }
    }

    public static void ValidateCommand(JsonElement payload)
    {
        var action = RequiredString(payload, "action");
        if (!CommandActions.Contains(action))
        {
            throw new ProtocolException($"Unknown command action '{action}'.");
        }

        switch (action)
        {
            case "requestSnapshot":
                return;
            case "setScene":
                RequiredDeck(payload);
                RequiredString(payload, "sceneId");
                return;
            case "setCrossfader":
                RequiredNumber(payload, "value", 0, 1);
                return;
            case "setBlackout":
            case "setPanicDim":
                RequiredBoolean(payload, "enabled");
                return;
            case "setEffect":
                RequiredDeck(payload);
                RequiredInteger(payload, "effectIndex", 0, int.MaxValue);
                RequiredNumber(payload, "amount", 0, 1);
                return;
            case "setModulation":
                RequiredDeck(payload);
                RequiredInteger(payload, "slot", 0, int.MaxValue);
                RequiredString(payload, "source");
                RequiredString(payload, "target");
                RequiredNumber(payload, "amount", -1, 1);
                RequiredNumber(payload, "smoothing", 0, 1);
                RequiredBoolean(payload, "enabled");
                return;
            case "saveCue":
            case "recallCue":
                RequiredInteger(payload, "index", 0, 15);
                return;
            case "setAudioSource":
                RequiredString(payload, "id");
                return;
            case "saveShow":
            case "loadShow":
                RequiredShowName(payload);
                return;
        }
    }

    public static int DecodeLength(ReadOnlySpan<byte> prefix)
    {
        if (prefix.Length != PrefixSize)
        {
            throw new ProtocolException("Frame prefix must be four bytes.");
        }

        var length = checked((int)BinaryPrimitives.ReadUInt32LittleEndian(prefix));
        if (length <= 0 || length > MaxMessageBytes)
        {
            throw new ProtocolException($"Frame length {length} is outside the permitted range.");
        }

        return length;
    }

    public static string DecodeUtf8(ReadOnlySpan<byte> bytes)
    {
        try
        {
            return new UTF8Encoding(false, true).GetString(bytes);
        }
        catch (DecoderFallbackException exception)
        {
            throw new ProtocolException("Frame body is not valid UTF-8.", exception);
        }
    }

    private static void RequiredDeck(JsonElement payload)
    {
        var deck = RequiredString(payload, "deck");
        if (deck is not ("A" or "B"))
        {
            throw new ProtocolException("deck must be 'A' or 'B'.");
        }
    }

    private static string RequiredString(JsonElement payload, string name)
    {
        if (!payload.TryGetProperty(name, out var value) || value.ValueKind != JsonValueKind.String)
        {
            throw new ProtocolException($"{name} must be a string.");
        }

        var text = value.GetString();
        if (string.IsNullOrWhiteSpace(text) || text.Length > 512)
        {
            throw new ProtocolException($"{name} must be a non-empty string no longer than 512 characters.");
        }

        return text;
    }

    private static void RequiredBoolean(JsonElement payload, string name)
    {
        if (!payload.TryGetProperty(name, out var value) ||
            value.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
        {
            throw new ProtocolException($"{name} must be a boolean.");
        }
    }

    private static void RequiredShowName(JsonElement payload)
    {
        var name = RequiredString(payload, "name");
        if (name.Length > 64 || !System.Text.RegularExpressions.Regex.IsMatch(name, "^[A-Za-z0-9][A-Za-z0-9 _-]*$"))
        {
            throw new ProtocolException("name must be 1..64 characters and contain only letters, numbers, spaces, '_' or '-'.");
        }
    }

    private static void RequiredNumber(JsonElement payload, string name, double minimum, double maximum)
    {
        if (!payload.TryGetProperty(name, out var value) ||
            value.ValueKind != JsonValueKind.Number ||
            !value.TryGetDouble(out var number) ||
            !double.IsFinite(number) ||
            number < minimum ||
            number > maximum)
        {
            throw new ProtocolException($"{name} must be a finite number in {minimum}..{maximum}.");
        }
    }

    private static void RequiredInteger(JsonElement payload, string name, int minimum, int maximum)
    {
        if (!payload.TryGetProperty(name, out var value) ||
            value.ValueKind != JsonValueKind.Number ||
            !value.TryGetInt32(out var number) ||
            number < minimum ||
            number > maximum)
        {
            throw new ProtocolException($"{name} must be an integer in {minimum}..{maximum}.");
        }
    }
}

public sealed class ProtocolException : Exception
{
    public ProtocolException(string message) : base(message)
    {
    }

    public ProtocolException(string message, Exception innerException) : base(message, innerException)
    {
    }
}
