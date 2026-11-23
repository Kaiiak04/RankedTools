using System;
using Newtonsoft.Json.Linq;

namespace RankedTools.Pcvr.Core;

public class PluginConfig
{
    public virtual string BeatLeaderPlayerId { get; set; } = "";
    public virtual string ScoreSaberPlayerId { get; set; } = "";
    public virtual string BeatLeaderPlayerName { get; set; } = "";
    public virtual string ScoreSaberPlayerName { get; set; } = "";
    public virtual string AccSaberPlayerId { get; set; } = "";
    public virtual string AccSaberPlayerName { get; set; } = "";
    public virtual string AccSaberCategory { get; set; } = "overall";
    public virtual bool EnableAccSaber { get; set; }
    public virtual int RequiredSyncScoreboards { get; set; } = 2;
    public static readonly string[] Scoreboards = { "BeatLeader", "ScoreSaber", "AccSaber" };
    public static readonly string[] AccSaberCategories = { "overall", "true_acc", "standard_acc", "tech_acc" };
    public static readonly string[] AccSaberCategoryLabels = { "Overall", "True Acc", "Standard Acc", "Tech Acc" };
    public static string NormalizeAccSaberCategory(string value) => Array.IndexOf(AccSaberCategories,value)>=0?value:"overall";
    public static int NormalizeRequiredSyncScoreboards(int value) => Math.Max(2,Math.Min(Scoreboards.Length,value));
    public virtual int MaxTracksPerPlaylist { get; set; } = 16;
    public virtual int MinClearRate { get; set; } = 50;
    public virtual bool SeparateNewAndRepeatTracks { get; set; }
    public virtual bool RecordLocalAttempts { get; set; } = true;
    public virtual bool ImportAttemptHistory { get; set; } = true;
    public virtual bool EnableBeatLeader { get; set; } = true;
    public virtual bool BeatLeaderModifiers { get; set; }
    public virtual bool EnableScoreSaber { get; set; } = true;
    public virtual bool EnableClanPlaylist { get; set; } = true;
    public virtual bool EnableSyncPlaylist { get; set; }
    public virtual bool LegacyClanPlaylists { get; set; }
    public virtual bool ClanPlaylistAllClans { get; set; }

    public JObject CaptureRefresh() => new()
    {
        ["command"] = "refresh", ["beatLeaderPlayerId"] = BeatLeaderPlayerId,
        ["scoreSaberPlayerId"] = ScoreSaberPlayerId,
        ["accSaberPlayerId"] = AccSaberPlayerId, ["accSaberCategory"] = NormalizeAccSaberCategory(AccSaberCategory),
        ["enableAccSaber"] = EnableAccSaber, ["requiredSyncScoreboards"] = NormalizeRequiredSyncScoreboards(RequiredSyncScoreboards),
        ["limit"] = Math.Max(1, Math.Min(100, MaxTracksPerPlaylist)),
        ["minClearRate"] = NormalizeClearRate(MinClearRate),
        ["separateNewAndRepeatTracks"] = SeparateNewAndRepeatTracks, ["importAttempts"] = ImportAttemptHistory,
        ["enableBeatLeader"] = EnableBeatLeader, ["enableScoreSaber"] = EnableScoreSaber,
        ["beatLeaderModifiers"] = BeatLeaderModifiers,
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
