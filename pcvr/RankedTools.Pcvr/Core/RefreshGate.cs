using System.Threading;

namespace RankedTools.Pcvr.Core;

internal sealed class RefreshGate
{
    private int running;
    public bool Busy => Volatile.Read(ref running) != 0;
    public bool TryEnter() => Interlocked.CompareExchange(ref running, 1, 0) == 0;
    public void Exit() => Interlocked.Exchange(ref running, 0);
}
