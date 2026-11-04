using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using IPA.Logging;
using IPA.Utilities;
using IPA.Utilities.Async;
using Newtonsoft.Json.Linq;
using RankedTools.Pcvr.Core;

namespace RankedTools.Pcvr;

internal sealed class Session
{
    public readonly PluginConfig Config;
    public readonly EngineClient Engine = new();
    private readonly Logger log;
    private readonly List<Task> workers = new();
    private readonly RefreshGate refreshGate = new();
    private bool initialized;
    public bool Stopping { get; private set; }
    public bool Busy => refreshGate.Busy;
    public JObject Snapshot { get; private set; } = new() { ["status"] = "Open RankedTools to refresh recommendations." };
    public event Action? Changed;

    public Session(PluginConfig config, Logger logger) { Config = config; log = logger; }
    public void Initialize()
    {
        if (initialized) return;
        try
        {
            var data = Path.Combine(UnityGame.UserDataPath, "RankedTools");
            Engine.Initialize(Path.Combine(UnityGame.NativeLibraryPath, "RankedTools.Core.dll"), data,
                BeatSaberPlaylistsLib.PlaylistManager.DefaultManager.PlaylistPath);
            initialized = true;
        }
        catch (Exception e) { Report("Could not initialize RankedTools: " + e.Message); }
    }
    public void Report(string text) { Snapshot["status"] = text; log.Info(text); Changed?.Invoke(); }
    public void Warn(string text) => log.Warn(text);
    public Task<JObject> Request(JObject request)
    {
        if (Stopping) throw new InvalidOperationException("RankedTools is shutting down.");
        if (!initialized) throw new InvalidOperationException("The RankedTools engine is unavailable. Reinstall the complete preview ZIP and check the log.");
        var task = Task.Run(() => Engine.Invoke(request));
        lock (workers) { workers.RemoveAll(x => x.IsCompleted); workers.Add(task); }
        return task;
    }
    public async Task Refresh()
    {
        if (!refreshGate.TryEnter()) return;
        try
        {
            // Capture configuration on the Unity thread before starting the worker.
            var request = Config.CaptureRefresh();
            Report("Fetching leaderboard data...");
            var task = Request(request);
            while (!task.IsCompleted && !Stopping)
            {
                await Task.Delay(300);
                if (Stopping) break;
                Snapshot = Engine.Invoke(new JObject { ["command"] = "snapshot" });
                Changed?.Invoke();
            }
            var result = await task;
            if (Stopping) return;
            Snapshot = result;
            foreach (var message in (result["logs"] as JArray ?? new JArray()).Values<string>()) log.Info(message ?? "");
            ReloadPlaylists(result);
        }
        catch (Exception e)
        {
            if (!Stopping)
            {
                // A later provider can fail after another provider saved output.
                try { Snapshot = Engine.Invoke(new JObject { ["command"] = "snapshot" }); ReloadPlaylists(Snapshot); } catch { }
                Report("Refresh stopped: " + e.Message);
            }
        }
        finally { refreshGate.Exit(); if (!Stopping) Changed?.Invoke(); }
    }
    private void ReloadPlaylists(JObject result)
    {
        if (result.Value<bool?>("filesChanged") != true) return;
        try { BeatSaberPlaylistsLib.PlaylistManager.DefaultManager.RequestRefresh("RankedTools"); }
        catch (Exception e) { log.Warn("Playlists were saved, but the browser could not reload them: " + e.Message); }
    }
    public void Record(JObject attempt)
    {
        if (!initialized || Stopping) return;
        var task = Request(attempt);
        _ = task.ContinueWith(t => log.Warn("Could not record attempt: " + t.Exception?.GetBaseException().Message), TaskContinuationOptions.OnlyOnFaulted);
    }
    public void Shutdown()
    {
        Stopping = true;
        try { Engine.Shutdown(); } catch (Exception e) { log.Warn(e.Message); }
        Task[] pending; lock (workers) pending = workers.Where(x => !x.IsCompleted).ToArray();
        try { if (!Task.WaitAll(pending, 5000)) log.Warn("Engine workers are still stopping; the native module will remain loaded until process exit."); }
        catch (AggregateException) { /* Outstanding cancelled requests are observed here. */ }
    }
}
