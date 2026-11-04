using System;
using System.Globalization;
using System.Text;
using BeatSaberMarkupLanguage.Attributes;
using HMUI;
using Newtonsoft.Json.Linq;
using RankedTools.Pcvr.Core;
using UnityEngine;

namespace RankedTools.Pcvr.UI;

internal sealed class RecommendationsHost : ObservableHost, IDisposable
{
    private readonly Session session;
    private int listIndex,songIndex,serviceIndex;
    private bool showAbility,disposed;
    private Texture2D? texture;
    private Sprite? sprite;
    private string chartError="";
    internal Action? Close { get; set; }
    [UIComponent("ability-image")] private ImageView image = null!;
    public RecommendationsHost(Session session) { this.session=session; session.Changed+=Changed; }
    private JArray Lists => session.Snapshot["playlists"] as JArray??new JArray();
    private JObject? List => Lists.Count==0?null:Lists[Math.Min(listIndex,Lists.Count-1)] as JObject;
    private JArray Entries => List?["entries"] as JArray??new JArray();
    private JObject? Song => Entries.Count==0?null:Entries[Math.Min(songIndex,Entries.Count-1)] as JObject;
    [UIValue("status")] public string Status => session.Snapshot.Value<string>("status")??"";
    [UIValue("can-refresh")] public bool CanRefresh => !session.Busy;
    [UIValue("show-details")] public bool ShowDetails => !showAbility;
    [UIValue("show-ability")] public bool ShowAbility => showAbility;
    [UIValue("has-chart")] public bool HasChart => showAbility && sprite!=null;
    [UIValue("details")] public string Details => showAbility?AbilityDetails():SongDetails();
    [UIValue("chart-notes")] public string ChartNotes => "Gray: eligible best scores (older scores are dimmer). Cyan: fitted accuracy. Shaded / dashed: outside observed stars.\nEstimates accuracy on a scored run, not clear probability or map-style-specific ability. No Fail and sub-55% scores are excluded.";
    public void Open() { Changed(); if(!session.Busy)Refresh(); }
    [UIAction("back")] private void Back() => Close?.Invoke();
    [UIAction("refresh")] private async void Refresh() { try { await session.Refresh(); } catch(Exception e) { if(!disposed)session.Report(e.Message); } }
    [UIAction("details-view")] private void DetailsView() { showAbility=false; Notify(); }
    [UIAction("ability-view")] private void AbilityView() { showAbility=true; UploadChart(); Notify(); }
    [UIAction("previous-list")] private void PreviousList() => ChangeList(-1);
    [UIAction("next-list")] private void NextList() => ChangeList(1);
    [UIAction("previous-song")] private void PreviousSong() => ChangeSong(-1);
    [UIAction("next-song")] private void NextSong() => ChangeSong(1);
    [UIAction("previous-service")] private void PreviousService() => ChangeService();
    [UIAction("next-service")] private void NextService() => ChangeService();
    private void ChangeList(int direction) { if(Lists.Count==0)return; listIndex=(listIndex+direction+Lists.Count)%Lists.Count; songIndex=0; Notify(); }
    private void ChangeSong(int direction) { if(Entries.Count==0)return; songIndex=(songIndex+direction+Entries.Count)%Entries.Count; Notify(); }
    private void ChangeService() { serviceIndex=1-serviceIndex; UploadChart(); Notify(); }
    private void Changed()
    {
        if(disposed || session.Stopping)return;
        if(showAbility && !session.Busy)UploadChart();
        Notify();
    }
    private string AbilityDetails()
    {
        if(chartError.Length>0)return "Could not display ability chart: "+chartError;
        var service=serviceIndex==0?"BeatLeader":"ScoreSaber";
        var enabled=session.Snapshot.Value<bool?>(serviceIndex==0?"enableBeatLeader":"enableScoreSaber") ?? (serviceIndex==0?session.Config.EnableBeatLeader:session.Config.EnableScoreSaber);
        if(!enabled)return service+" ability estimate\nEnable this leaderboard's playlists, then refresh.";
        var a=(session.Snapshot["abilities"] as JArray)?[serviceIndex];
        if(a?.Value<bool?>("ready")!=true)return service+" ability estimate\nRefresh recommendations to load profile scores.";
        if(!string.IsNullOrEmpty(a.Value<string>("error")))return service+" ability estimate\n"+a.Value<string>("error");
        if((a.Value<int?>("scoreCount")??0)==0)return service+" ability estimate\nNo eligible ranked Standard scores to fit yet. No Fail and sub-55% scores are excluded.";
        return service+" ability estimate\n"+a.Value<int>("scoreCount")+" eligible best scores. Played range: "+F(a.Value<double>("minStars"),2)+" to "+F(a.Value<double>("maxStars"),2)+" stars.";
    }
    private static string F(double value,int decimals=1) => value.ToString("F"+decimals,CultureInfo.InvariantCulture);
    private string SongDetails()
    {
        var list=List; if(list==null)return "Enable a leaderboard, clan or Sync playlist in Mod Settings, then refresh.";
        var text=new StringBuilder(); text.AppendLine(list.Value<string>("title")); text.AppendLine(list.Value<string>("note"));
        var error=list.Value<string>("error"); if(!string.IsNullOrEmpty(error))return text+"Previous playlist preserved. "+error;
        if(list.Value<bool?>("ready")!=true)return text+"Waiting for recommendations...";
        var song=Song; if(song==null)return text+"No eligible songs found for this playlist.";
        var difficulty=song["difficulties"]?.First;
        text.AppendLine("Song "+(Math.Min(songIndex,Entries.Count-1)+1)+" of "+Entries.Count);
        text.AppendLine(song.Value<string>("songName"));
        if(list.Value<int>("kind")==3)
        {
            text.AppendLine("Ranked on both leaderboards. All matching charts; no song limit.");
            foreach(var target in (song["syncTargets"] as JArray) ?? new JArray())
                text.AppendLine(target.Value<string>("characteristic")+" / "+target.Value<string>("difficulty")+": missing on "+target.Value<string>("missingService"));
            return text.ToString();
        }
        text.AppendLine((difficulty?.Value<string>("name")??"")+" | "+F(difficulty?.Value<double?>("stars")??0,2)+" stars");
        text.AppendLine("Predicted accuracy: "+F(song.Value<double>("predictedAccuracy")*100)+"%");
        text.AppendLine("Predicted score: "+F(song.Value<double>("predictedPp"))+" PP");
        var minClearRate=session.Snapshot.Value<int?>("minClearRate")??PluginConfig.NormalizeClearRate(session.Config.MinClearRate);
        if(list.Value<bool?>("clan")==true || list.Value<int>("kind")!=1)
            text.AppendLine("Min Clear Rate: "+minClearRate+"%");
        var improvement=song.Value<bool>("isImprovement"); var clan=song["clanImpact"];
        if(list.Value<bool?>("clan")==true)
        {
            text.AppendLine(improvement?"Improve an existing score":"New contribution");
            text.AppendLine("Current personal score: "+F(song.Value<double>("currentPp"))+" PP");
            text.AppendLine("Rough clear chance: "+F(song.Value<double>("clearChance")*100,0)+"%");
            if(clan!=null)
            {
                text.AppendLine("Remaining clan gap: "+F(clan.Value<double>("gap"),2)+" weighted PP");
                text.AppendLine("Projected clan gain: +"+F(clan.Value<double>("gain"),2)+" weighted PP");
                text.AppendLine(clan.Value<bool>("captures")?"Capture at predicted score":"Useful progress toward capture");
                if(clan["requiredPp"]!=null && clan["requiredAccuracy"]!=null)
                    text.AppendLine("Capture target: "+F(Math.Ceiling(clan.Value<double>("requiredPp")*100)/100,2)+" personal PP / "+F(Math.Ceiling(clan.Value<double>("requiredAccuracy")*10000)/100,2)+"% accuracy (unmodified)");
                else text.AppendLine("Capture exceeds the theoretical unmodified maximum.");
                text.AppendLine("Capture is an estimate, not a calibrated probability.");
            }
            else
            {
                text.AppendLine("Current clan gap: "+F(Math.Max(0,-song.Value<double>("priority")),2)+" weighted PP");
                text.AppendLine("Sorted by the current clan gap; personal PP filters completed targets.");
            }
        }
        else
        {
            if(improvement)text.AppendLine("Current score: "+F(song.Value<double>("currentPp"))+" PP");
            else text.AppendLine("Rough clear chance: "+F(song.Value<double>("clearChance")*100,0)+"%");
            text.AppendLine("Weighted PP gain if "+(improvement?"improved":"cleared")+": +"+F(song.Value<double>("weightedPpGain"),2));
            if(list.Value<int>("kind")==0 && minClearRate>0)
                text.AppendLine("Chance-adjusted gain: +"+F(song.Value<double>("weightedPpGain")*song.Value<double>("clearChance"),2));
            if(list.Value<int>("kind")==2)text.AppendLine("Sorted by weighted PP gain");
        }
        var confidence=song.Value<double>("confidence");
        text.AppendLine("Confidence: "+(confidence>=.7?"High":confidence>=.5?"Medium":"Low"));
        text.Append("Nearby history: "+song.Value<int>("nearbyClears")+" clears, "+song.Value<int>("nearbyFailures")+" failures, "+song.Value<int>("nearbyNoFail")+" NF runs");
        return text.ToString();
    }
    private void UploadChart()
    {
        ReleaseChart();
        chartError="";
        if(disposed || !showAbility || session.Busy || image==null)return;
        try
        {
            var pixels=session.Engine.Chart(serviceIndex,out var width,out var height); if(pixels==null)return;
            texture=new Texture2D(width,height,TextureFormat.RGBA32,false);
            texture.LoadRawTextureData(pixels); texture.Apply(false,true);
            sprite=Sprite.Create(texture,new Rect(0,0,width,height),new Vector2(.5f,.5f),100);
            image.sprite=sprite;
        }
        catch(Exception e) { chartError=e.Message; session.Warn("Could not display ability chart: "+e.Message); }
    }
    private void ReleaseChart()
    {
        if(image!=null)image.sprite=null;
        if(sprite!=null)UnityEngine.Object.Destroy(sprite); if(texture!=null)UnityEngine.Object.Destroy(texture);
        sprite=null; texture=null;
    }
    public void Dispose() { disposed=true; session.Changed-=Changed; ReleaseChart(); }
}
