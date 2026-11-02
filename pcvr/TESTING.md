# PCVR preview tester checklist

Please test **1.40.8 and 1.44.1 separately**. Automated build/test success is recorded in each ZIP; both still need in-game checks.

Record your game version, RankedTools ZIP name, Steam or Oculus PC, and installed BSIPA/BSML/SiraUtil/SongCore/BeatSaberPlaylistsLib/playlist-browser versions. For 1.44.1, include the playlist browser's exact source, since the documented community build is not in the verified mod list.

- [ ] Extract the complete ZIP. Start the game twice; confirm the RankedTools main-menu button and Mod Settings entry appear without startup errors.
- [ ] Search for BeatLeader and ScoreSaber accounts by name, profile URL, and numeric ID. Check page buttons and explicit selection. Start a search, change the input or service, then confirm an old response cannot replace the new selection. Check avatars, clear/cancel, show/hide IDs, and the current-PC-account button.
- [ ] Change settings, restart, and confirm account names/IDs, track limit, clear rate, toggles, and clan mode persist.
- [ ] Refresh with each provider independently, then both. Click refresh repeatedly; only one refresh should run. Change a setting during refresh; it should apply to the next one.
- [ ] Check Not Played and To Improve, then switch Simple Mode on/off. Confirm generated playlists reload in your browser, only incompatible generated layouts disappear, covers and selected difficulties are correct, and your own playlists remain intact.
- [ ] In your playlist browser, open a generated playlist and download a missing map. If that fails, report whether the browser can load/download an unrelated playlist too.
- [ ] Check personalized clans, legacy mode, main clan, and all clans. Secondary clans must say what-if primary clan. Legacy 0% clear rate must disable its maximum star cap. Confirm clan covers and useful replay/capture explanations.
- [ ] Select both accounts and enable Sync. Check its supplied cover, a chart scored only on ScoreSaber and one only on BeatLeader, and a non-Standard chart where available. Charts scored on both or neither must be absent. Set song count to 1: Sync must still contain every match. Check multi-difficulty songs and each missing-provider label. Disable personal playlists and confirm Sync still refreshes. Post the missing score, wait for the public API to update, then refresh and confirm that chart disappears. Disconnect during refresh and confirm the previous Sync file survives.
- [ ] Cycle through playlists and songs in Song details. Check long names and descriptions, scrolling, numeric predictions, confidence, and clan targets. No text should overlap or be clipped beyond scrolling.
- [ ] Open both ability charts, including a sparse/empty profile. Check chart orientation, gray points/cyan fit, extrapolation, explanations, and repeated view changes without stale images or errors.
- [ ] With your own IDs configured, record a normal custom-song clear, failure, quit, restart, practice run, and modifier run. Confirm expected records appear under `UserData/RankedTools/attempts/`. Change the configured account after a song starts; the result must remain associated with the IDs captured at its start.
- [ ] Watch a BeatLeader replay and a ScoreSaber replay. Confirm neither adds a local attempt. Check the log for replay-state compatibility warnings if a normal play is unexpectedly skipped.
- [ ] Disconnect the network and refresh an existing setup. Confirm affected previous playlists remain available and the error is readable. Reconnect and confirm refresh recovers.
- [ ] Enter gameplay while a refresh runs, return to the menu, then exit the game. Confirm no duplicate buttons, stale views, hanging shutdown, or Unity-thread errors.

For a problem, include the latest Beat Saber log from `Logs/`, the steps that caused it, and whether it reproduces with other optional mods disabled. Screenshots of layout problems help. Remove private information from logs before sharing. Avoid posting entire score/attempt caches unless needed.

Report 1.40.8 and 1.44.1 results independently; success on one does not verify the other.
