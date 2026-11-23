using Newtonsoft.Json.Linq;

namespace RankedTools.Pcvr.Core;

// Owned by the Unity thread. Every input/service/cancel change invalidates both
// pending replies and old result buttons, even when the HTTP request completes.
internal sealed class AccountLookup
{
    private long revision;
    public bool Loading { get; private set; }
    public JObject? Result { get; private set; }
    public bool IsCurrent(long request) => request == revision;
    public long Begin() { Loading = true; Result = null; return ++revision; }
    public void Invalidate() { ++revision; Loading = false; Result = null; }
    public bool Complete(long request, JObject result)
    {
        if (request != revision) return false;
        Loading = false; Result = result; return true;
    }
    public JObject? Select(long request, int index)
    {
        if (request != revision || Loading || index < 0) return null;
        return (Result?["profiles"] as JArray)?.Count > index ? Result!["profiles"]![index] as JObject : null;
    }
}
