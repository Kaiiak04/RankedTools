using System.Runtime.InteropServices;
using System.Text;
using System.Xml.Linq;
using Mono.Cecil;
using Newtonsoft.Json.Linq;
using RankedTools.Pcvr.Core;

static class Program
{
    static int checks;
    static void Check(bool condition,string message)
    {
        checks++; if(!condition)throw new InvalidOperationException(message);
    }
    static void Throws(Action action,string message)
    {
        try { action(); } catch(InvalidOperationException) { checks++; return; }
        throw new InvalidOperationException(message);
    }
    static void Models()
    {
        var config=new PluginConfig(); var captured=config.CaptureRefresh();
        Check(captured.Value<int>("limit")==16 && captured.Value<int>("minClearRate")==50,"defaults");
        Check(captured.Value<bool>("importAttempts") && !captured.Value<bool>("legacyClanPlaylists"),"mode defaults");
        Check(!captured.Value<bool>("enableSyncPlaylist"),"Sync is opt-in");
        config.EnableSyncPlaylist=true;
        Check(config.CaptureRefresh().Value<bool>("enableSyncPlaylist"),"Sync setting reaches native request");
        config.BeatLeaderPlayerId="76561198000000001"; config.MaxTracksPerPlaylist=1000; config.MinClearRate=53;
        var snapshot=config.CaptureRefresh(); config.BeatLeaderPlayerId="456"; config.MaxTracksPerPlaylist=2;
        Check(snapshot.Value<string>("beatLeaderPlayerId")=="76561198000000001" && snapshot.Value<int>("limit")==100,"immutable settings and exact ID");
        Check(snapshot.Value<int>("minClearRate")==55,"clear-rate rounding");
        for(var i=0;i<=95;i+=5)Check(PluginConfig.NormalizeClearRate(i)==i,"clear-rate step");
        Check(PluginConfig.NormalizeClearRate(-100)==0 && PluginConfig.NormalizeClearRate(1000)==95,"clear-rate bounds");
        Check(PluginConfig.NormalizeClearRate(int.MinValue)==0 && PluginConfig.NormalizeClearRate(int.MaxValue)==95,"extreme edited settings");
        var lookup=new AccountLookup(); var old=lookup.Begin(); var current=lookup.Begin();
        var page=new JObject { ["profiles"]=new JArray(new JObject { ["id"]="76561198000000001" }) };
        Check(!lookup.Complete(old,page),"superseded search reply");
        Check(lookup.Complete(current,page) && lookup.Select(current,0)?.Value<string>("id")=="76561198000000001","explicit account selection");
        lookup.Invalidate(); Check(lookup.Select(current,0)==null && !lookup.Complete(current,page),"cancel invalidates results and replies");
        var gate=new RefreshGate(); var winners=0;
        Parallel.For(0,32,_=> { if(gate.TryEnter())Interlocked.Increment(ref winners); });
        Check(winners==1 && gate.Busy,"overlapping refreshes"); gate.Exit(); Check(gate.TryEnter(),"refresh can run after completion"); gate.Exit();
        var profile=new RecordingProfile(config); config.BeatLeaderPlayerId="789";
        var level="custom_level_"+new string('a',40);
        JObject? Attempt(bool solo=true,bool invalid=false,bool replay=false,bool practice=false,string state="Cleared",string action="None") =>
            AttemptClassifier.Capture(profile,level,"Standard",4,solo,invalid,replay,practice,true,"",state,action,120);
        Check(Attempt()?.Value<string>("beatLeaderPlayerId")=="456","attempt profile frozen at start");
        Check(Attempt()?.Value<int>("outcome")==1,"clear classification");
        Check(Attempt(state:"Failed")?.Value<int>("outcome")==2,"failure classification");
        Check(Attempt(state:"None",action:"Quit")?.Value<int>("outcome")==3,"quit classification");
        Check(Attempt(state:"None",action:"Restart")?.Value<int>("outcome")==4,"restart classification");
        Check(Attempt(practice:true)?.Value<int>("outcome")==5,"practice classification");
        Check(Attempt(solo:false)==null && Attempt(invalid:true)==null && Attempt(replay:true)==null,"gameplay exclusions");
        Check(AttemptClassifier.Capture(profile,"custom_level_bad","Standard",4,true,false,false,false,true,"","Cleared","None",2)==null,"malformed hash excluded");
        Check(ReplayGuard.Read("BeatLeader",_=>typeof(FakeBeatLeader))==ReplayStatus.Live,"BeatLeader live guard");
        FakeBeatLeader.IsStartedAsReplay=true;
        Check(ReplayGuard.Read("BeatLeader",_=>typeof(FakeBeatLeader))==ReplayStatus.Replay,"BeatLeader replay guard");
        Check(ReplayGuard.Read("ScoreSaber",name=>name.EndsWith("Registry")?typeof(FakeRegistry):null)==ReplayStatus.Replay,"current ScoreSaber replay guard");
        Check(ReplayGuard.Read("ScoreSaber",name=>name=="ScoreSaber.Plugin"?typeof(FakeOldPlugin):null)==ReplayStatus.Live,"verified ScoreSaber 3.3.17 live guard");
        FakeOldPlugin.ReplayState.IsPlaybackEnabled=true;
        Check(ReplayGuard.Read("ScoreSaber",name=>name=="ScoreSaber.Plugin"?typeof(FakeOldPlugin):null)==ReplayStatus.Replay,"verified ScoreSaber 3.3.17 replay guard");
        Check(ReplayGuard.Read("ScoreSaber",_=>null)==ReplayStatus.Unknown,"unknown replay integration fails closed");
    }
    static void Bindings(string plugin)
    {
        using var assembly=AssemblyDefinition.ReadAssembly(plugin);
        var manifest=assembly.MainModule.Resources.OfType<EmbeddedResource>().Single(r=>r.Name=="RankedTools.Pcvr.manifest.json");
        var json=JObject.Parse(Encoding.UTF8.GetString(manifest.GetResourceData()));
        var game=json.Value<string>("gameVersion");
        Check(game is "1.40.8" or "1.44.1","target manifest");
        // Inspect the actual pinned PC BSML tag registry without loading Unity.
        // XML validity and member bindings alone do not detect unsupported tags.
        using var bsml=AssemblyDefinition.ReadAssembly(Path.Combine(".cache/pcvr/refs-"+game,"Plugins/BSML.dll"));
        var tags=bsml.MainModule.Types.Where(t=>t.Namespace=="BeatSaberMarkupLanguage.Tags" || t.Namespace.StartsWith("BeatSaberMarkupLanguage.Tags.",StringComparison.Ordinal))
            .SelectMany(t=>t.Methods.Where(m=>m.Name=="get_Aliases" && m.HasBody))
            .SelectMany(m=>m.Body.Instructions.Where(i=>i.OpCode.Code==Mono.Cecil.Cil.Code.Ldstr))
            .Select(i=>(string)i.Operand).ToHashSet(StringComparer.Ordinal);
        Check(tags.Contains("vertical") && tags.Contains("scrollable-settings-container"),"pinned BSML tag registry");
        foreach(var name in new[]{"Settings","Recommendations"})
        {
            var host=assembly.MainModule.Types.Single(t=>t.FullName=="RankedTools.Pcvr.UI."+name+"Host");
            string? Binding(IEnumerable<CustomAttribute> attributes,string suffix) => attributes.FirstOrDefault(a=>a.AttributeType.Name==suffix)?.ConstructorArguments.FirstOrDefault().Value as string;
            var values=host.Properties.Select(p=>Binding(p.CustomAttributes,"UIValue")).Where(x=>x!=null).ToHashSet();
            var actions=host.Methods.Select(p=>Binding(p.CustomAttributes,"UIAction")).Where(x=>x!=null).ToHashSet();
            var components=host.Fields.Select(p=>Binding(p.CustomAttributes,"UIComponent")).Where(x=>x!=null).ToHashSet();
            var resource=assembly.MainModule.Resources.OfType<EmbeddedResource>().Single(r=>r.Name=="RankedTools.Pcvr.Views."+name+".bsml");
            var xml=XDocument.Parse(Encoding.UTF8.GetString(resource.GetResourceData()));
            foreach(var element in xml.Descendants())
                Check(tags.Contains(element.Name.LocalName),name+" uses an unregistered PC BSML tag <"+element.Name.LocalName+">");
            foreach(var attribute in xml.Descendants().Attributes())
            {
                var value=attribute.Value; var key=attribute.Name.LocalName;
                if(value.StartsWith('~'))Check(values.Contains(value.Substring(1)),name+" missing value "+value);
                else if(key is "on-click" or "formatter")Check(actions.Contains(value),name+" missing action "+value);
                else if(key=="value")Check(values.Contains(value),name+" missing setting "+value);
                else if(key is "text" or "active" or "interactable")Check(!values.Contains(value),name+" needs ~ on dynamic property "+value);
            }
            foreach(var element in xml.Descendants().Where(e=>e.Attribute("value")!=null))
            {
                var id=element.Attribute("value")!.Value;
                var property=host.Properties.Single(p=>Binding(p.CustomAttributes,"UIValue")==id);
                var integerOnly=(bool?)element.Attribute("integer-only")==true;
                var expected=element.Name.LocalName switch
                {
                    "slider-setting" or "increment-setting" => integerOnly?"System.Int32":"System.Single",
                    "toggle-setting" => "System.Boolean",
                    "string-setting" => "System.String",
                    _ => null,
                };
                if(expected==null)continue;
                Check(property.PropertyType.FullName==expected,name+" setting "+id+" uses "+property.PropertyType.FullName+"; "+element.Name.LocalName+" requires "+expected);
                Check(property.GetMethod!=null && property.SetMethod!=null,name+" setting "+id+" needs a getter and setter");
                if(element.Attribute("formatter") is { } format)
                {
                    var formatter=host.Methods.Single(m=>Binding(m.CustomAttributes,"UIAction")==format.Value);
                    Check(formatter.Parameters.Count==1 && formatter.Parameters[0].ParameterType.FullName==expected && formatter.ReturnType.FullName=="System.String",name+" formatter "+format.Value+" must accept "+expected+" and return a string");
                }
            }
            foreach(var component in components)Check(xml.Descendants().Count(e=>e.Attribute("id")?.Value==component)==1,name+" missing component "+component);
            if(name=="Settings")
            {
                var query=xml.Descendants("string-setting").Single(e=>(string?)e.Attribute("value")=="search-input");
                Check((bool?)query.Attribute("apply-on-change")==true,"account query must reach the host before Find is clicked");
                Check((bool?)query.Attribute("bind-value")==true,"account query must reflect clear/cancel and account-picker changes");
            }
        }
    }
    static void Native(string dll,bool live,bool badCa)
    {
        var root=Path.GetFullPath("tests/pcvr-sandbox/managed-Å-测试"+(badCa?"-bad-ca":""));
        Check(!Directory.Exists(root),"fresh native sandbox"); Directory.CreateDirectory(root);
        var native=Path.Combine(root,"RankedTools.Core.dll"); File.Copy(Path.GetFullPath(dll),native);
        File.WriteAllText(Path.Combine(root,"cacert.pem"),badCa?"not a certificate":File.ReadAllText("cacert.pem"));
        var engine=new EngineClient(); engine.Initialize(native,root,Path.Combine(root,"Playlists"));
        for(var i=0;i<250;i++)Check(engine.Invoke(new JObject{["command"]="snapshot"}).Value<bool>("ok"),"native response allocation/free");
        Throws(()=>engine.Invoke(new JObject{["command"]="unknown"}),"unknown command must fail");
        Check(engine.Chart(-1,out _,out _)==null,"empty chart transfer");
        var refresh=new JObject{["command"]="refresh",["enableBeatLeader"]=false,["enableScoreSaber"]=false,["enableClanPlaylist"]=false};
        Check(!engine.Invoke(refresh).Value<bool>("succeeded"),"empty configuration gives guidance");
        var attempt=new JObject{["command"]="attempt",["beatLeaderPlayerId"]="76561198000000001",["scoreSaberPlayerId"]="",["id"]="first",["hash"]=new string('a',40),["characteristic"]="Standard",["difficulty"]="Expert",["timestamp"]=1700000000,["endTime"]=100,["outcome"]=1,["normal"]=true,["practice"]=false};
        engine.Invoke(attempt); attempt["id"]="second"; attempt["timestamp"]=1700000300; engine.Invoke(attempt);
        var history=JObject.Parse(File.ReadAllText(Path.Combine(root,"attempts/beatleader_76561198000000001.json")));
        Check((history["attempts"] as JArray)?.Count==2,"managed-to-native attempt persistence and Unicode paths");
        if(live)
        {
            foreach(var url in new[]{"https://api.beatleader.com/player/76561198000000000","https://scoresaber.com/api/player/76561198000000000/basic"})
            {
                var probe=engine.Invoke(new JObject{["command"]="httpsProbe",["url"]=url});
                Check((probe.Value<string>("tlsBackend")??"").Contains("mbedTLS",StringComparison.OrdinalIgnoreCase),"packaged TLS implementation");
                // A public 404 still proves a completed, verified HTTPS handshake.
                var success=probe.Value<bool>("success"); var error=probe.Value<string>("error")??"";
                Check(badCa?!success && !error.Contains("HTTP "):success||error.Contains("HTTP "),"packaged-engine HTTPS: "+url+" "+error);
            }
            Check(!engine.Invoke(new JObject{["command"]="httpsProbe",["url"]="http://api.beatleader.com/"}).Value<bool>("success"),"plain HTTP rejected");
        }
        engine.Shutdown();
        // Windows keeps a loaded native DLL mapped until this process exits.
        // The caller removes this explicitly named sandbox after process exit.
    }
    static int Main(string[] args)
    {
        try
        {
            if(args.Contains("--bad-ca"))Native(args[Array.IndexOf(args,"--native")+1],true,true);
            else
            {
                Models();
                for(var i=0;i<args.Length;i++)if(args[i]=="--plugin")Bindings(args[i+1]);
                if(args.Contains("--native"))Native(args[Array.IndexOf(args,"--native")+1],args.Contains("--live"),false);
            }
            Console.WriteLine($"{checks} managed / binding / native interop checks passed."); return 0;
        }
        catch(Exception e){Console.Error.WriteLine(e);return 1;}
    }
}
internal static class FakeBeatLeader { public static bool IsStartedAsReplay { get; set; } }
internal static class FakeRegistry { internal static bool IsPlaybackEnabled => true; }
internal sealed class FakeReplayState { internal bool IsPlaybackEnabled; }
internal static class FakeOldPlugin { internal static FakeReplayState ReplayState { get; } = new(); }
