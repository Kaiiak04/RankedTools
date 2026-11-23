using HarmonyLib;
using IPA;
using IPA.Config;
using IPA.Config.Stores;
using IPA.Logging;
using RankedTools.Pcvr.Core;
using RankedTools.Pcvr.UI;
using SiraUtil.Zenject;
using Zenject;

namespace RankedTools.Pcvr;

[Plugin(RuntimeOptions.SingleStartInit)]
public sealed class Plugin
{
    internal static Session? Session { get; private set; }
    private readonly Harmony harmony = new("Kaiiak04.RankedTools.PCVR");

    [Init]
    public Plugin(Logger logger, Config config, Zenjector zenjector)
    {
        Session = new Session(config.Generated<PluginConfig>(), logger);
        zenjector.Install<MenuInstaller>(Location.Menu, Session);
    }
    [OnStart] public void Start() => harmony.PatchAll(typeof(Plugin).Assembly);
    [OnExit] public void Exit() { Session?.Shutdown(); harmony.UnpatchSelf(); }
}

internal sealed class MenuInstaller : Installer
{
    private readonly Session session;
    public MenuInstaller(Session session) { this.session = session; }
    public override void InstallBindings()
    {
        Container.BindInstance(session);
        Container.BindInterfacesAndSelfTo<MenuCoordinator>().AsSingle();
    }
}
