using System;
using System.Reflection;

namespace RankedTools.Pcvr.Core;

internal enum ReplayStatus { Live, Replay, Unknown }
internal static class ReplayGuard
{
    private const BindingFlags Static = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static;
    private const BindingFlags Instance = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance;
    // Only reads replay booleans. Account selection uses public platform identity
    // and public HTTP APIs; authentication state/tokens are never inspected.
    public static ReplayStatus Read(string assemblyName, Func<string,Type?> findType)
    {
        try
        {
            if(assemblyName=="BeatLeader")
            {
                var value=findType("BeatLeader.Replayer.ReplayerLauncher")?.GetProperty("IsStartedAsReplay",Static)?.GetValue(null,null);
                return value is bool replay ? replay?ReplayStatus.Replay:ReplayStatus.Live : ReplayStatus.Unknown;
            }
            if(assemblyName=="ScoreSaber")
            {
                var registry=findType("ScoreSaber.Features.Replays.ReplayStateRegistry");
                var current=registry?.GetProperty("IsPlaybackEnabled",Static)?.GetValue(null,null);
                if(current is bool playing)return playing?ReplayStatus.Replay:ReplayStatus.Live;
                // Verified ScoreSaber 3.3.17 stores the replay state on Plugin.
                var state=findType("ScoreSaber.Plugin")?.GetProperty("ReplayState",Static)?.GetValue(null,null);
                if(state==null)return ReplayStatus.Unknown;
                var value=state.GetType().GetField("IsPlaybackEnabled",Instance)?.GetValue(state);
                return value is bool replay ? replay?ReplayStatus.Replay:ReplayStatus.Live : ReplayStatus.Unknown;
            }
            return ReplayStatus.Live;
        }
        catch { return ReplayStatus.Unknown; }
    }
}
