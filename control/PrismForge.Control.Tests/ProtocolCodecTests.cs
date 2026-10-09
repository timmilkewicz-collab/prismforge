using System.Buffers.Binary;
using System.Text.Json;
using PrismForge.Control;
using Xunit;

namespace PrismForge.Control.Tests;

public sealed class ProtocolCodecTests
{
    [Fact]
    public void MaxMessageBytes_MatchesEngineV1()
    {
        Assert.Equal(65_536, ProtocolCodec.MaxMessageBytes);
    }

    [Fact]
    public void Encode_WritesLittleEndianLengthAndRoundTrips()
    {
        var envelope = Command("{\"action\":\"setCrossfader\",\"value\":0.5}");

        var frame = ProtocolCodec.Encode(envelope);
        var length = BinaryPrimitives.ReadUInt32LittleEndian(frame.AsSpan(0, 4));
        var body = ProtocolCodec.DecodeUtf8(frame.AsSpan(4));
        var decoded = ProtocolCodec.Decode(body);

        Assert.Equal(frame.Length - 4, (int)length);
        Assert.Equal("Command", decoded.Type);
        Assert.Equal("setCrossfader", decoded.Payload.GetProperty("action").GetString());
        Assert.Equal(0.5, decoded.Payload.GetProperty("value").GetDouble());
    }

    [Fact]
    public void Decode_AllowsStateSnapshotWithoutRequestId()
    {
        var envelope = ProtocolCodec.Decode("""
            {"version":1,"type":"StateSnapshot","payload":{"revision":3}}
            """);

        Assert.Null(envelope.RequestId);
        Assert.Equal(3, envelope.Payload.GetProperty("revision").GetInt32());
    }

    [Fact]
    public void ValidateCommand_RejectsUnknownAction()
    {
        var envelope = Command("{\"action\":\"launchMissiles\"}");

        var exception = Assert.Throws<ProtocolException>(() => ProtocolCodec.ValidateEnvelope(envelope));
        Assert.Contains("Unknown command action", exception.Message);
    }

    [Theory]
    [InlineData(-0.1)]
    [InlineData(1.1)]
    public void ValidateCommand_RejectsCrossfaderOutsideRange(double value)
    {
        var envelope = Command($"{{\"action\":\"setCrossfader\",\"value\":{value}}}");

        Assert.Throws<ProtocolException>(() => ProtocolCodec.ValidateEnvelope(envelope));
    }

    [Fact]
    public void ValidateCommand_AcceptsModulationRoute()
    {
        var envelope = Command("""
            {
              "action":"setModulation",
              "deck":"B",
              "slot":2,
              "source":"audio.low",
              "target":"deck.effect.1",
              "amount":-0.75,
              "smoothing":0.2,
              "enabled":true
            }
            """);

        ProtocolCodec.ValidateEnvelope(envelope);
    }

    [Theory]
    [InlineData(0, 0.0)]
    [InlineData(3, 1.0)]
    public void ValidateCommand_AcceptsMasterPerformanceMacros(int index, double amount)
    {
        var envelope = Command($"{{\"action\":\"setMasterEffect\",\"index\":{index},\"amount\":{amount}}}");

        ProtocolCodec.ValidateEnvelope(envelope);
    }

    [Theory]
    [InlineData(0, 0.0)]
    [InlineData(3, 1.0)]
    public void ValidateCommand_AcceptsSceneParameter(int index, double amount)
    {
        var envelope = Command($"{{\"action\":\"setSceneParameter\",\"deck\":\"A\",\"sceneId\":\"mirror-cathedral\",\"index\":{index},\"amount\":{amount}}}");

        ProtocolCodec.ValidateEnvelope(envelope);
    }

    [Theory]
    [InlineData(-1, 0.5)]
    [InlineData(4, 0.5)]
    [InlineData(0, -0.1)]
    [InlineData(0, 1.1)]
    public void ValidateCommand_RejectsSceneParameterOutsideRange(int index, double amount)
    {
        var envelope = Command($"{{\"action\":\"setSceneParameter\",\"deck\":\"A\",\"sceneId\":\"mirror-cathedral\",\"index\":{index},\"amount\":{amount}}}");

        Assert.Throws<ProtocolException>(() => ProtocolCodec.ValidateEnvelope(envelope));
    }

    [Theory]
    [InlineData(-1, 0.5)]
    [InlineData(4, 0.5)]
    [InlineData(0, -0.1)]
    [InlineData(0, 1.1)]
    public void ValidateCommand_RejectsMasterPerformanceMacroOutsideRange(int index, double amount)
    {
        var envelope = Command($"{{\"action\":\"setMasterEffect\",\"index\":{index},\"amount\":{amount}}}");

        Assert.Throws<ProtocolException>(() => ProtocolCodec.ValidateEnvelope(envelope));
    }

    [Fact]
    public void DecodeLength_RejectsOversizedFrames()
    {
        var prefix = new byte[4];
        BinaryPrimitives.WriteUInt32LittleEndian(prefix, (uint)ProtocolCodec.MaxMessageBytes + 1u);

        Assert.Throws<ProtocolException>(() => ProtocolCodec.DecodeLength(prefix));
    }

    [Theory]
    [InlineData("Festival Set 01")]
    [InlineData("bass_night-final")]
    public void ValidateCommand_AcceptsSafeShowNames(string name)
    {
        var envelope = Command($"{{\"action\":\"saveShow\",\"name\":\"{name}\"}}");

        ProtocolCodec.ValidateEnvelope(envelope);
    }

    [Theory]
    [InlineData("../escape")]
    [InlineData("bad:name")]
    [InlineData(" leading")]
    public void ValidateCommand_RejectsUnsafeShowNames(string name)
    {
        var envelope = Command($"{{\"action\":\"loadShow\",\"name\":\"{name}\"}}");

        Assert.Throws<ProtocolException>(() => ProtocolCodec.ValidateEnvelope(envelope));
    }

    private static ProtocolEnvelope Command(string payloadJson)
    {
        using var document = JsonDocument.Parse(payloadJson);
        return new ProtocolEnvelope(1, "Command", Guid.NewGuid().ToString(), document.RootElement.Clone());
    }
}
