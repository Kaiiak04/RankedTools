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
    private bool closing;

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
        recommendations.Close = Close;
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
        if(!button.Interactable)return;
        button.Interactable=false;
        try
        {
            if (flow == null)
            {
                flow = BeatSaberUI.CreateFlowCoordinator<RankedToolsFlow>();
                flow.Back = Close;
            }
            // Build markup before HMUI begins its transition and disables input.
            // A parser failure must leave the existing menu usable.
            flow.Prepare(recommendations);
            main.PresentFlowCoordinator(flow, () =>
            {
                try { recommendations.Open(); }
                catch(Exception e) { session.Warn("RankedTools screen update failed: "+e); }
            });
        }
        catch(Exception e)
        {
            button.Interactable=true;
            session.Warn("Could not open RankedTools: "+e);
            session.Report("Could not open RankedTools. See the game log for details.");
        }
    }
    private void Close()
    {
        if(flow==null || closing)return;
        closing=true;
        try { main.DismissFlowCoordinator(flow, () => { closing=false; button.Interactable=true; }); }
        catch(Exception e) { closing=false; session.Warn("Could not close RankedTools: "+e); }
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
    internal Action Back = null!;
    private ViewController? view;
    internal void Prepare(RecommendationsHost host)
    {
        if(view!=null)return;
        var candidate=BeatSaberUI.CreateViewController<ViewController>();
        try
        {
            Legato.Presentation.BsmlParser.Instance.Parse(
                Utilities.GetResourceContent(typeof(Plugin).Assembly,"RankedTools.Pcvr.Views.Recommendations.bsml"),
                candidate.gameObject,host);
            view=candidate;
        }
        catch
        {
            UnityEngine.Object.Destroy(candidate.gameObject);
            throw;
        }
    }
    protected override void DidActivate(bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling)
    {
        if (!firstActivation) return;
        SetTitle("RankedTools"); showBackButton = true;
        ProvideInitialViewControllers(view);
    }
    protected override void BackButtonWasPressed(ViewController topViewController) => Back();
}
