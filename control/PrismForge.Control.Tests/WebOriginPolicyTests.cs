using PrismForge.Control;
using Xunit;

namespace PrismForge.Control.Tests;

public sealed class WebOriginPolicyTests
{
    [Theory]
    [InlineData("https://control.prismforge.local/")]
    [InlineData("https://CONTROL.PRISMFORGE.LOCAL/index.html")]
    [InlineData("https://control.prismforge.local/assets/index.js")]
    public void IsAllowed_AcceptsBundledVirtualHost(string source)
    {
        Assert.True(WebOriginPolicy.IsAllowed(source));
    }

    [Theory]
    [InlineData("http://control.prismforge.local/index.html")]
    [InlineData("https://example.com/")]
    [InlineData("https://control.prismforge.local.example.com/")]
    [InlineData("https://control.prismforge.local:444/index.html")]
    [InlineData("https://user@control.prismforge.local/index.html")]
    [InlineData("not a URI")]
    [InlineData("")]
    public void IsAllowed_RejectsNonLocalOrigins(string source)
    {
        Assert.False(WebOriginPolicy.IsAllowed(source));
    }
}
