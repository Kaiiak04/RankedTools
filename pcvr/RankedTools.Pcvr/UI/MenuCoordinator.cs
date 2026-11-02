using System;
using System.Threading;
using System.Threading.Tasks;
using BeatSaberMarkupLanguage;
using BeatSaberMarkupLanguage.MenuButtons;
using BeatSaberMarkupLanguage.Settings;
using HMUI;
using Legato.Platform;
using UnityEngine;
using Zenject;
#if BEAT_SABER_1_42_0
using OculusStudios.Platform.Core;
#endif

namespace RankedTools.Pcvr.UI;

internal sealed class MenuCoordinator : IInitializable, IDisposable
{
    private readonly Session session;
    private readonly BSMLSettings settings;
    private readonly MenuButtons buttons;
    private readonly MainFlowCoordinator main;
    private readonly SettingsHost settingsHost;
    private readonly RecommendationsHost recommendations;
    private readonly MenuButton button;
    private RankedToolsFlow? flow;

    [Inject]
    public MenuCoordinator(Session session, BSMLSettings settings, MenuButtons buttons, MainFlowCoordinator main,
#if BEAT_SABER_1_42_0
        IPlatform platform)
#else
        IPlatformUserModel platform)
#endif
    {
        this.session=session; this.settings=settings; this.buttons=buttons; this.main=main;
        var adapter = new GamePlatformAdapter(platform);
        settingsHost = new SettingsHost(session, async () => (await adapter.GetUserInfo(CancellationToken.None)).platformUserId);
        recommendations = new RecommendationsHost(session);
        button = new MenuButton("RankedTools", "Refresh ranked and clan playlists", Open);
    }
    public void Initialize()
    {
        session.Initialize();
        settings.AddSettingsMenu("RankedTools", "RankedTools.Pcvr.Views.Settings.bsml", settingsHost);
        buttons.RegisterButton(button);
    }
    private void Open()
    {
        if (flow == null)
        {
            flow = BeatSaberUI.CreateFlowCoordinator<RankedToolsFlow>();
            flow.Host = recommendations;
            flow.Back = () => main.DismissFlowCoordinator(flow, () => button.Interactable=true);
        }
        if(!button.Interactable)return;
        button.Interactable=false;
        try { main.PresentFlowCoordinator(flow); }
        catch(Exception e) { button.Interactable=true; session.Report("Could not open RankedTools: "+e.Message); }
    }
    public void Dispose()
    {
        buttons.UnregisterButton(button); settings.RemoveSettingsMenu(settingsHost);
        settingsHost.Dispose(); recommendations.Dispose();
        if (flow != null) UnityEngine.Object.Destroy(flow.gameObject);
    }
}

internal sealed class RankedToolsFlow : FlowCoordinator
{
    internal RecommendationsHost Host = null!;
    internal Action Back = null!;
    private RankedToolsView? view;
    protected override void DidActivate(bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling)
    {
        if (!firstActivation) return;
        SetTitle("RankedTools"); showBackButton = true;
        view = BeatSaberUI.CreateViewController<RankedToolsView>(); view.Host = Host;
        ProvideInitialViewControllers(view);
    }
    protected override void BackButtonWasPressed(ViewController topViewController) => Back();
}
internal sealed class RankedToolsView : ViewController
{
    internal RecommendationsHost Host = null!;
    protected override void DidActivate(bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling)
    {
        if (firstActivation)
            Legato.Presentation.BsmlParser.Instance.Parse(Utilities.GetResourceContent(typeof(Plugin).Assembly,"RankedTools.Pcvr.Views.Recommendations.bsml"),gameObject,Host);
        Host.Open();
    }
}
