using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using HarmonyLib;
using RankedTools.Pcvr.Core;

namespace RankedTools.Pcvr;

// The two target builds use their own game references and Legato profile. Game
// objects are read only here; none cross the unmanaged boundary.
internal static class GameAdapter
{
    private static bool warnedReplay;
    internal static bool IsReplay()
    {
        foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies())
        {
            var name=assembly.GetName().Name??"";
            if(name!="BeatLeader" && name!="ScoreSaber")continue;
            var state=ReplayGuard.Read(name,type=>assembly.GetType(type,false));
            if(state==ReplayStatus.Unknown && !warnedReplay)
            {
                warnedReplay=true;
                Plugin.Session?.Warn("Could not verify "+name+" replay state. Local attempt recording is skipped for this session; imported history remains available.");
            }
            if(state!=ReplayStatus.Live)return true;
        }
        return false;
    }
    internal static string Modifiers(GameplayModifiers modifiers)
    {
        var tokens = new List<string>();
        void Add(bool enabled, string token) { if (enabled) tokens.Add(token); }
        Add(modifiers.noFailOn0Energy,"NF"); Add(modifiers.zenMode,"ZEN"); Add(modifiers.instaFail,"IF");
        Add(modifiers.failOnSaberClash,"SC"); Add(modifiers.noBombs,"NB"); Add(modifiers.noArrows,"NA");
        Add(modifiers.ghostNotes,"GN"); Add(modifiers.disappearingArrows,"DA"); Add(modifiers.fastNotes,"FN");
        Add(modifiers.proMode,"PM"); Add(modifiers.smallCubes,"SCUB"); Add(modifiers.strictAngles,"SA");
        Add((int)modifiers.songSpeed==1,"FS"); Add((int)modifiers.songSpeed==2,"SS"); Add((int)modifiers.songSpeed==3,"SF");
        Add((int)modifiers.energyType!=0,"BE"); Add((int)modifiers.enabledObstacleType!=0,"NO");
        tokens.Sort(StringComparer.Ordinal);
        return tokens.Count == 0 ? "MODIFIED" : string.Join(",", tokens);
    }
}

[HarmonyPatch(typeof(GameplayCoreInstaller), "InstallBindings")]
internal static class BeginAttemptPatch
{
    internal static RecordingProfile? Profile;
    internal static bool Replay;
    [HarmonyPrefix]
    private static void Begin()
    {
        Profile = null; Replay = true;
        try
        {
            var session = Plugin.Session;
            Replay = GameAdapter.IsReplay();
            if (session != null && !session.Stopping && session.Config.RecordLocalAttempts && !Replay)
                Profile = new RecordingProfile(session.Config);
        }
        catch(Exception e) { Profile = null; Plugin.Session?.Warn("Could not start attempt capture: "+e.Message); }
    }
}

[HarmonyPatch(typeof(StandardLevelScenesTransitionSetupDataSO), "Finish")]
internal static class EndAttemptPatch
{
    [HarmonyPrefix]
    private static void Finish(StandardLevelScenesTransitionSetupDataSO __instance, LevelCompletionResults __0)
    {
        var profile = BeginAttemptPatch.Profile; BeginAttemptPatch.Profile = null;
        try
        {
            if (profile == null || __instance == null || __0 == null) return;
            var key = __instance.beatmapKey; var modifiers = __0.gameplayModifiers;
            var normal = modifiers != null && modifiers.IsWithoutModifiers();
            var attempt = AttemptClassifier.Capture(profile, key.levelId, key.beatmapCharacteristic?.serializedName ?? "",
                (int)key.difficulty, __instance.gameMode == "Solo", __0.invalidated,
                BeginAttemptPatch.Replay || GameAdapter.IsReplay(), __instance.practiceSettings != null,
                normal, normal ? "" : modifiers != null ? GameAdapter.Modifiers(modifiers) : "MODIFIED",
                __0.levelEndStateType.ToString(), __0.levelEndAction.ToString(), __0.endSongTime);
            if (attempt != null) Plugin.Session?.Record(attempt);
        }
        catch(Exception e) { Plugin.Session?.Warn("Could not capture attempt: "+e.Message); }
    }
}
