namespace RankedTools.Pcvr.Core;

internal static class RefreshStatusText
{
    public static string Summary(string status)
    {
        var end=status.IndexOf('\n');
        return (end<0?status:status.Substring(0,end)).TrimEnd('\r');
    }
}
