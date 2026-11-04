using System;
using System.Linq;
using Newtonsoft.Json.Linq;

namespace RankedTools.Pcvr.Core;

internal sealed class RecordingProfile
{
    public readonly string BeatLeader, ScoreSaber;
    public RecordingProfile(PluginConfig config) { BeatLeader = config.BeatLeaderPlayerId; ScoreSaber = config.ScoreSaberPlayerId; }
}
internal static class AttemptClassifier
{
    private static readonly string[] Difficulties = { "Easy", "Normal", "Hard", "Expert", "ExpertPlus" };
    public static JObject? Capture(RecordingProfile? profile, string levelId, string characteristic, int difficulty,
        bool solo, bool invalidated, bool replay, bool practice, bool normal, string modifiers, string endState, string endAction, double endTime)
    {
        if (profile == null || !solo || invalidated || replay || levelId.Length != 53 || !levelId.StartsWith("custom_level_", StringComparison.Ordinal) ||
            !levelId.Substring(13).All(Uri.IsHexDigit) || difficulty < 0 || difficulty >= Difficulties.Length || string.IsNullOrEmpty(characteristic)) return null;
        var outcome = practice ? 5 : endState == "Failed" ? 2 : endAction == "Restart" ? 4 : endAction == "Quit" ? 3 : endState == "Cleared" ? 1 : 0;
        var now = (DateTime.UtcNow - new DateTime(1970, 1, 1)).TotalSeconds;
        return new JObject
        {
            ["command"] = "attempt", ["beatLeaderPlayerId"] = profile.BeatLeader, ["scoreSaberPlayerId"] = profile.ScoreSaber,
            ["id"] = Guid.NewGuid().ToString("N"), ["hash"] = levelId.Substring(13), ["characteristic"] = characteristic,
            ["difficulty"] = Difficulties[difficulty], ["timestamp"] = now, ["endTime"] = Math.Max(0, endTime),
            ["practice"] = practice, ["normal"] = normal, ["modifiers"] = modifiers, ["outcome"] = outcome,
        };
    }
}
