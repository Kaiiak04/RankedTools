using System;
using System.Threading.Tasks;
using BeatSaberMarkupLanguage.Attributes;
using HMUI;
using Legato.Presentation;
using Newtonsoft.Json.Linq;
using RankedTools.Pcvr.Core;
using UnityEngine;

namespace RankedTools.Pcvr.UI;

internal sealed class SettingsHost : ObservableHost, IDisposable
{
    private readonly Session session;
    private readonly Func<Task<string>> currentUser;
    private readonly AccountLookup lookup = new();
    private readonly Sprite?[] sprites = new Sprite?[5];
    private string service="BeatLeader", input="", message="";
    private bool picker, showIds, disposed;
    private long rowRevision;
    private PluginConfig Config => session.Config;
    [UIComponent("avatar-0")] private ImageView avatar0 = null!;
    [UIComponent("avatar-1")] private ImageView avatar1 = null!;
    [UIComponent("avatar-2")] private ImageView avatar2 = null!;
    [UIComponent("avatar-3")] private ImageView avatar3 = null!;
    [UIComponent("avatar-4")] private ImageView avatar4 = null!;
    public SettingsHost(Session session, Func<Task<string>> currentUser) { this.session=session; this.currentUser=currentUser; }

    [UIValue("beatleader-account")] public string BeatLeaderAccount => AccountLabel("BeatLeader",Config.BeatLeaderPlayerId,Config.BeatLeaderPlayerName);
    [UIValue("scoresaber-account")] public string ScoreSaberAccount => AccountLabel("ScoreSaber",Config.ScoreSaberPlayerId,Config.ScoreSaberPlayerName);
    private string AccountLabel(string name,string id,string selected) => name+": "+(id.Length==0?"Not selected":selected.Length==0?"Saved account":selected)+(showIds&&id.Length>0?"\nID: "+id:"");
    [UIValue("picker-open")] public bool PickerOpen => picker;
    [UIValue("picker-title")] public string PickerTitle => service+" account";
    [UIValue("lookup-status")] public string LookupStatus => message;
    [UIValue("search-input")] public string SearchInput
    {
        get => input;
        set { if(input==value)return; input=value; lookup.Invalidate(); ReleaseAvatars(); message=""; Notify(); }
    }
    [UIValue("can-find")] public bool CanFind => !lookup.Loading;
    [UIValue("can-previous")] public bool CanPrevious => !lookup.Loading && (lookup.Result?.Value<int?>("page")??1)>1;
    [UIValue("can-next")] public bool CanNext => !lookup.Loading && lookup.Result?.Value<bool?>("hasNext")==true;
    [UIValue("has-0")] public bool Has0 => Profile(0)!=null;
    [UIValue("has-1")] public bool Has1 => Profile(1)!=null;
    [UIValue("has-2")] public bool Has2 => Profile(2)!=null;
    [UIValue("has-3")] public bool Has3 => Profile(3)!=null;
    [UIValue("has-4")] public bool Has4 => Profile(4)!=null;
    [UIValue("row-0")] public string Row0 => Row(0);
    [UIValue("row-1")] public string Row1 => Row(1);
    [UIValue("row-2")] public string Row2 => Row(2);
    [UIValue("row-3")] public string Row3 => Row(3);
    [UIValue("row-4")] public string Row4 => Row(4);
    private JObject? Profile(int index) => lookup.Select(rowRevision,index);
    private string Row(int index)
    {
        var profile=Profile(index); if(profile==null)return "";
        var rank=profile.Value<long?>("rank")??0;
        return profile.Value<string>("name")+"\n"+(profile.Value<string>("country")??"Country unknown")+" | "+(rank>0?"Rank #"+rank:"Unranked")+"\nID: "+profile.Value<string>("id");
    }
    [UIValue("track-limit")] public int TrackLimit { get=>Config.MaxTracksPerPlaylist; set=>Config.MaxTracksPerPlaylist=Math.Max(1,Math.Min(100,value)); }
    [UIValue("clear-rate")] public float ClearRate { get=>PluginConfig.NormalizeClearRate(Config.MinClearRate); set=>Config.MinClearRate=PluginConfig.NormalizeClearRate((int)value); }
    [UIAction("clear-rate-format")] private string FormatClearRate(float value) => PluginConfig.NormalizeClearRate((int)value)+"%";
    [UIValue("simple-mode")] public bool SimpleMode { get=>Config.SimpleMode; set=>Config.SimpleMode=value; }
    [UIValue("record-attempts")] public bool RecordAttempts { get=>Config.RecordLocalAttempts; set=>Config.RecordLocalAttempts=value; }
    [UIValue("import-attempts")] public bool ImportAttempts { get=>Config.ImportAttemptHistory; set=>Config.ImportAttemptHistory=value; }
    [UIValue("enable-beatleader")] public bool EnableBeatLeader { get=>Config.EnableBeatLeader; set=>Config.EnableBeatLeader=value; }
    [UIValue("enable-scoresaber")] public bool EnableScoreSaber { get=>Config.EnableScoreSaber; set=>Config.EnableScoreSaber=value; }
    [UIValue("enable-clans")] public bool EnableClans { get=>Config.EnableClanPlaylist; set=>Config.EnableClanPlaylist=value; }
    [UIValue("legacy-clans")] public bool LegacyClans { get=>Config.LegacyClanPlaylists; set=>Config.LegacyClanPlaylists=value; }
    [UIValue("all-clans")] public bool AllClans { get=>Config.ClanPlaylistAllClans; set=>Config.ClanPlaylistAllClans=value; }

    [UIAction("choose-beatleader")] private void ChooseBeatLeader() => Choose("BeatLeader");
    [UIAction("choose-scoresaber")] private void ChooseScoreSaber() => Choose("ScoreSaber");
    private void Choose(string next) { service=next; picker=true; lookup.Invalidate(); input=""; message="Enter a display name, profile URL, or numeric ID."; ReleaseAvatars(); Notify(); }
    [UIAction("toggle-ids")] private void ToggleIds() { showIds=!showIds; Notify(); }
    [UIAction("cancel")] private void Cancel() { lookup.Invalidate(); picker=false; ReleaseAvatars(); Notify(); }
    [UIAction("clear-account")] private void ClearAccount()
    {
        if(service=="BeatLeader") { Config.BeatLeaderPlayerId=""; Config.BeatLeaderPlayerName=""; }
        else { Config.ScoreSaberPlayerId=""; Config.ScoreSaberPlayerName=""; }
        Cancel();
    }
    [UIAction("find")] private async void Find() => await Search(1);
    [UIAction("previous-page")] private async void Previous() => await Search(Math.Max(1,(lookup.Result?.Value<int?>("page")??1)-1));
    [UIAction("next-page")] private async void Next() => await Search((lookup.Result?.Value<int?>("page")??1)+1);
    private async Task Search(int page)
    {
        var request=lookup.Begin(); rowRevision=request; ReleaseAvatars(); message="Finding accounts..."; Notify();
        await Lookup(request,input,page,false);
    }
    [UIAction("use-pc-account")] private async void UsePcAccount()
    {
        var request=lookup.Begin(); rowRevision=request; ReleaseAvatars(); message="Checking the current PC account..."; Notify();
        try
        {
            var id=await currentUser();
            if(disposed || !lookup.IsCurrent(request))return;
            if(string.IsNullOrEmpty(id))throw new InvalidOperationException("No platform account is available. Use account search instead.");
            await Lookup(request,id,1,true);
        }
        catch(Exception e) { if(!disposed && lookup.Complete(request,new JObject())) { message=e.Message; Notify(); } }
    }
    private async Task Lookup(long request,string query,int page,bool autoSelect)
    {
        var requestedService=service;
        try
        {
            var result=await session.Request(new JObject { ["command"]="accounts",["service"]=requestedService,["input"]=query,["page"]=page });
            if(disposed || !lookup.Complete(request,result))return;
            if(autoSelect && (result["profiles"] as JArray)?.Count==1) { Select(0); return; }
            message=(result["profiles"] as JArray)?.Count>0 ? "Page "+page+": select your account below." : "No matching accounts found.";
            Notify();
            for(var i=0;i<5;i++)
            {
                var profile=lookup.Select(request,i); if(profile==null)break;
                try
                {
                    var avatar=await session.Request(new JObject { ["command"]="avatar",["url"]=profile.Value<string>("avatar")??"" });
                    if(disposed || lookup.Select(request,i)?.Value<string>("id")!=profile.Value<string>("id"))return;
                    var text=avatar.Value<string>("image")??""; var comma=text.IndexOf(','); if(comma<0)continue;
                    sprites[i]=SpriteFactory.Create(Convert.FromBase64String(text.Substring(comma+1)));
                    var image=Avatar(i); if(image!=null)image.sprite=sprites[i];
                }
                catch { /* Avatars are optional; selection remains available. */ }
            }
        }
        catch(Exception e) { if(!disposed && lookup.Complete(request,new JObject())) { message=e.Message; Notify(); } }
    }
    [UIAction("select-0")] private void Select0() => Select(0);
    [UIAction("select-1")] private void Select1() => Select(1);
    [UIAction("select-2")] private void Select2() => Select(2);
    [UIAction("select-3")] private void Select3() => Select(3);
    [UIAction("select-4")] private void Select4() => Select(4);
    private void Select(int index)
    {
        var profile=Profile(index); if(profile==null)return;
        var id=profile.Value<string>("id")??""; var name=profile.Value<string>("name")??"";
        if(service=="BeatLeader") { Config.BeatLeaderPlayerId=id; Config.BeatLeaderPlayerName=name; }
        else { Config.ScoreSaberPlayerId=id; Config.ScoreSaberPlayerName=name; }
        Cancel();
    }
    [UIAction("#post-parse")] private async void Parsed()
    {
        foreach(var provider in new[]{"BeatLeader","ScoreSaber"})
        {
            var id=provider=="BeatLeader"?Config.BeatLeaderPlayerId:Config.ScoreSaberPlayerId; if(id.Length==0)continue;
            try
            {
                var result=await session.Request(new JObject { ["command"]="accounts",["service"]=provider,["input"]=id,["page"]=1 });
                if(disposed)return;
                var profile=result["profiles"]?[0];
                if(profile?.Value<string>("id")!=id)continue;
                if(provider=="BeatLeader" && Config.BeatLeaderPlayerId==id)Config.BeatLeaderPlayerName=profile.Value<string>("name")??"";
                if(provider=="ScoreSaber" && Config.ScoreSaberPlayerId==id)Config.ScoreSaberPlayerName=profile.Value<string>("name")??"";
                Notify();
            }
            catch { /* Preserve the saved account and name on API failure. */ }
        }
    }
    private ImageView Avatar(int i) => i==0?avatar0:i==1?avatar1:i==2?avatar2:i==3?avatar3:avatar4;
    private void ReleaseAvatars()
    {
        for(var i=0;i<sprites.Length;i++) { if(Avatar(i)!=null)Avatar(i).sprite=null; if(sprites[i]!=null)SpriteFactory.Destroy(sprites[i]!); sprites[i]=null; }
    }
    public void Dispose() { disposed=true; lookup.Invalidate(); ReleaseAvatars(); }
}
