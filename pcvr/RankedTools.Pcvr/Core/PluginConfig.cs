using System;
using Newtonsoft.Json.Linq;

namespace RankedTools.Pcvr.Core;

public class PluginConfig
{
    public virtual string BeatLeaderPlayerId { get; set; } = "";
    public virtual string ScoreSaberPlayerId { get; set; } = "";
    public virtual string BeatLeaderPlayerName { get; set; } = "";
    public virtual string ScoreSaberPlayerName { get; set; } = "";
    public virtual int MaxTracksPerPlaylist { get; set; } = 16;
    public virtual int MinClearRate { get; set; } = 50;
    public virtual bool SimpleMode { get; set; }
    public virtual bool RecordLocalAttempts { get; set; } = true;
    public virtual bool ImportAttemptHistory { get; set; } = true;
    public virtual bool EnableBeatLeader { get; set; } = true;
    public virtual bool EnableScoreSaber { get; set; } = true;
    public virtual bool EnableClanPlaylist { get; set; } = true;
    public virtual bool EnableSyncPlaylist { get; set; }
    public virtual bool LegacyClanPlaylists { get; set; }
    public virtual bool ClanPlaylistAllClans { get; set; }

    public JObject CaptureRefresh() => new()
    {
        ["command"] = "refresh", ["beatLeaderPlayerId"] = BeatLeaderPlayerId,
        ["scoreSaberPlayerId"] = ScoreSaberPlayerId,
        ["limit"] = Math.Max(1, Math.Min(100, MaxTracksPerPlaylist)),
        ["minClearRate"] = NormalizeClearRate(MinClearRate),
        ["simpleMode"] = SimpleMode, ["importAttempts"] = ImportAttemptHistory,
        ["enableBeatLeader"] = EnableBeatLeader, ["enableScoreSaber"] = EnableScoreSaber,
        ["enableClanPlaylist"] = EnableClanPlaylist, ["legacyClanPlaylists"] = LegacyClanPlaylists,
        ["enableSyncPlaylist"] = EnableSyncPlaylist,
        ["allClans"] = ClanPlaylistAllClans,
    };

    public static int NormalizeClearRate(int value)
    {
        var bounded = Math.Max(0, Math.Min(95, value));
        return ((bounded + 2) / 5) * 5;
    }
}
