namespace PrismForge.Control;

public static class WebOriginPolicy
{
    public const string VirtualHost = "control.prismforge.local";

    public static bool IsAllowed(string? source)
    {
        return Uri.TryCreate(source, UriKind.Absolute, out var uri) && IsAllowed(uri);
    }

    public static bool IsAllowed(Uri uri)
    {
        return uri.Scheme.Equals(Uri.UriSchemeHttps, StringComparison.OrdinalIgnoreCase) &&
               uri.Host.Equals(VirtualHost, StringComparison.OrdinalIgnoreCase) &&
               uri.IsDefaultPort &&
               string.IsNullOrEmpty(uri.UserInfo);
    }
}
