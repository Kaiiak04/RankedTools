# RankedTools 0.1.28 testing checklist

For Quest and PCVR testers of the 0.1.28 preview release. The maintainer reported
the Quest HTTPS-fix build working as intended on November 13, 2026; the full
checklist and current PCVR builds still benefit from wider testing. AccSaber
clan playlists are deferred; their supplied cover is reserved for later.

## Install and record

- [ ] Record your platform, game version, package filename and existing RankedTools version.
- [ ] Close Beat Saber and keep a copy of your existing RankedTools settings and generated playlists.
- [ ] **Quest:** use Beat Saber **1.40.8_7379** and install `RankedTools-0.1.28.qmod` with your usual mod manager. Keep PlaylistManager and the QMOD's required dependencies installed. Its bytes match the previously tested `RankedTools-0.1.28-https-fix-testing.qmod`.
- [ ] **PC:** use Beat Saber **1.40.8**. Install BSIPA, BSML, SiraUtil, SongCore and BeatSaberPlaylistsLib for that version, plus PlaylistManager to browse/download songs. Extract **all contents** of `RankedTools-PCVR-0.1.28-preview.1-bs1.40.8.zip` into the folder containing `Beat Saber.exe`, allowing RankedTools files to be replaced. The ZIP contains `Plugins`, `Libs` and `UserData`; do not copy just the plugin DLL.
- [ ] Start the game. RankedTools opens from the main menu and its settings open without errors, overlapping controls or inaccessible buttons.

Use your own accounts while **Record local attempts** is enabled. Turn recording
off before selecting another person's profile. No login tokens or passwords
are needed in RankedTools.

## Essential checks — both testers

### Defaults and existing playlists

- [ ] **Separate New and Repeat Tracks** starts **off**. Refresh with an existing BeatLeader/ScoreSaber account: one **PP Gain** playlist appears per enabled service. The old Simple Mode toggle is gone.
- [ ] Turn separation **on**, refresh, and check **Not Played** and **To Improve**. Turn it off and refresh again. Only incompatible RankedTools personal playlists disappear; your own playlists, downloaded songs and clan playlists survive.
- [ ] BeatLeader and ScoreSaber still refresh, show predictions and load their covers. If you use BeatLeader clans, check one clan refresh and both personalized/legacy modes for an obvious regression.
- [ ] Close/reopen the menu, then restart the game. Accounts, toggles, category and Sync requirement remain saved.

Existing saved settings remain in place. If a setting was already changed on
your installation, record its initial value rather than clearing all settings.

### AccSaber account and personal playlists

- [ ] **Create AccSaber playlists** starts **off**, and **AccSaber Category** starts **Overall**.
- [ ] Find your AccSaber account by display name, select the intended result, and confirm the displayed name/ID. Also try your numeric ID or `https://accsaber.com/players/<id>` URL. If an ID is linked/merged, confirm the saved profile is the intended canonical account.
- [ ] Enable AccSaber and refresh. With separation off, **AccSaber - AP Gain** appears with the supplied Simple Mode cover. Song details say **AP**, **complexity**, and the correct category, with finite accuracy/gain numbers.
- [ ] Try **Overall**, **True Acc**, **Standard Acc** and **Tech Acc**, refreshing after each change. Single-category playlists contain that category and identify it in the title. Changing category replaces the same playlist instead of leaving duplicates.
- [ ] Turn separation on: AccSaber **Not Played** and **To Improve** appear with their supplied covers. Return to off: only the combined AP Gain playlist remains.
- [ ] Download/open a recommendation. Its song, characteristic and difficulty match the preview. The track limit is respected, without duplicate songs in a personal playlist.
- [ ] Compare a few displayed existing AP values/categories/complexities with your AccSaber profile. Predicted AP is an estimate for the displayed future accuracy, not a promise of your next score.
- [ ] Open **Ability Chart**. Cycle through the enabled services and AccSaber categories. AccSaber charts use **Complexity** and separate category fits. Check that the graph appears and stays visible after switching views, refreshing and reopening the menu.
- [ ] If a category has no eligible scores, it explains the missing evidence. It preserves that category selection's previous playlist; Overall can use other categories with evidence.

If AccSaber requests fail, record the **exact error** and whether your AccSaber
profile loads in a normal browser. A public-profile AccSaber refresh and live API responses were checked before
packaging. Your device/account flow still needs testing; an API failure should
preserve old output.

### Sync

Sync uses **every selected account**, even if that service's personal playlists
are disabled. A chart needs a posted score on at least one qualifying service;
fully unplayed charts do not belong in Sync. AccSaber's categories count as one
scoreboard. Imports into AccSaber can take time to appear.

- [ ] Select **two** accounts and set **Required Sync Scoreboards = 2**. Enable Sync and refresh. Use BeatLeader/ScoreSaber first if AccSaber is unavailable.
- [ ] Inspect a chart scored on one selected service and missing on another. It is in Sync, with the correct missing service, hash/characteristic/difficulty, and the new supplied Sync cover. Charts scored on all participating services, or on none, are absent.
- [ ] With three selected accounts, try requirement **2**, then **3**. At 3, every chart must be ranked on all three services. At 2, a chart ranked on any qualifying pair can appear. Record a concrete example if available.
- [ ] If a chart is missing on **two** of its ranked services, both missing services appear in the preview, with one song/difficulty entry.
- [ ] With only two selected accounts, requirement **3** gives setup guidance and preserves the previous Sync playlist.
- [ ] Disable personal playlists, set the track limit to **1**, and change AccSaber Category/Min Clear Rate. Sync still includes **all** qualifying charts and categories; it is not cut down to one song.
- [ ] If available, check a non-Standard chart and a posted NF score. Exact chart identity is preserved, NF counts as posted, and an unfinished/local attempt does not count as a posted score.
- [ ] Play a missing chart, wait for the service's public profile/import to update, and refresh. Its missing-service label disappears; the chart leaves Sync once no qualifying service is missing it.

## Failure and recording checks — once per platform

- [ ] Restart the game and refresh with your usual accounts and Sync enabled. Repeat two more times. If a transfer fails, the final status and saved `last-refresh-error.log` should include the curl number and available TLS details; a later successful refresh must preserve that file.
- [ ] After a successful refresh, disconnect the network and refresh again. Failed services explain the error and preserve their previous playlists. Reconnect and confirm refresh recovers.
- [ ] With your own IDs selected, play one ordinary custom-song clear and one failure. Refresh and check nearby-history evidence where available. Local records stay associated with the account selected **when the song started**.
- [ ] If convenient, change the AccSaber account during a song, then finish it: its attempt must stay with the starting account. Restore your own account afterward.
- [ ] Scroll to the end of long song details, history and warnings. Navigation and Back remain usable. On PC, also verify no stale/missing Ability Chart image after repeated view changes.

## Send back results

Send this completed checklist, marking **pass / fail / not tested**, plus:

- Platform, exact game version and package filename.
- The setting combination and selected category/Sync requirement.
- Exact error text, a screenshot if useful, and one song hash + characteristic + difficulty for incorrect results.
- Expected result versus actual result; whether retrying/restarting changes it.
- Approximate first/warm refresh times and whether settings survived restart.
- **PC:** the relevant `Logs` entries (including `_latest.log` where present). Settings are in `UserData/RankedTools.json`; local history is under `UserData/RankedTools/attempts/`; playlists normally live in `Playlists/`.
- **Quest:** the relevant RankedTools/`beatleaderpp` log output or a logcat capture if available. Local history and assets live under `/sdcard/ModData/com.beatgames.beatsaber/Mods/RankedPractice/`.

Account IDs are public, but you can redact them before sharing. Do not send
passwords, tokens or unrelated personal logs.
