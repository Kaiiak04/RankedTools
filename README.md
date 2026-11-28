# RankedTools

RankedTools is a Beat Saber mod for Quest and Windows PCVR that creates personalized BeatLeader, ScoreSaber, and AccSaber playlists. Find ranked maps suited to your ability, improve existing scores, and fill gaps across leaderboards.

## Download

**RankedTools 0.1.31 is an experimental prerelease.** It adds ranked OneSaber recommendations and includes optional BeatLeader modifier recommendations. [Release notes and files](https://github.com/Kaiiak04/RankedTools/releases/tag/v0.1.31)

| Platform | Beat Saber version | Download |
| --- | --- | --- |
| Quest | `1.40.8_7379` | [QMOD](https://github.com/Kaiiak04/RankedTools/releases/download/v0.1.31/RankedTools-0.1.31.qmod) |
| Windows x64 PCVR preview | `1.40.8` | [ZIP](https://github.com/Kaiiak04/RankedTools/releases/download/v0.1.31/RankedTools-PCVR-0.1.31-preview.1-bs1.40.8.zip) |
| Windows x64 PCVR preview | `1.44.1` | [ZIP](https://github.com/Kaiiak04/RankedTools/releases/download/v0.1.31/RankedTools-PCVR-0.1.31-preview.1-bs1.44.1.zip) |

The earlier Quest modifier build was confirmed in game. In-game checks for OneSaber playlist selection on Quest and for both current PCVR packages are pending. [PCVR tester checklist](https://github.com/Kaiiak04/RankedTools/blob/v0.1.31/pcvr/TESTING.md) · [Build validation](https://github.com/Kaiiak04/RankedTools/releases/download/v0.1.31/BUILD-VALIDATION.json) · [SHA-256 checksums](https://github.com/Kaiiak04/RankedTools/releases/download/v0.1.31/SHA256SUMS.txt)

## Getting started

1. **Install for your game version.** On Quest, install the QMOD and PlaylistManager with your mod manager. On Windows, install the required mods, close Beat Saber, and extract the matching ZIP into the folder containing `Beat Saber.exe`. Use a compatible playlist browser to open the generated playlists and download missing songs. [PCVR install and playlist browser compatibility](https://github.com/Kaiiak04/RankedTools/blob/v0.1.31/pcvr/README.md#install)
2. **Choose your accounts in Mod Settings.** Search BeatLeader, ScoreSaber, or AccSaber by display name, profile URL, or numeric ID, then select the matching profile. RankedTools fetches scores through public APIs. On Quest, **Use signed-in BeatLeader account** can fill in your ID from a compatible BeatLeader mod.
3. **Choose your playlists.** Set **Tracks per playlist** and **Min Clear Rate**. For AccSaber, turn on **Create AccSaber playlists** and choose Overall, True Acc, Standard Acc, or Tech Acc. [AccSaber setup and scoring](https://github.com/Kaiiak04/RankedTools/blob/v0.1.31/docs/ACCSABER.md)
4. **Open RankedTools from the main menu** to refresh. Use **Song details** to see the selected chart, predicted accuracy and PP or AP, and estimated gain. **Ability chart** shows your eligible scores and the fitted curve. On Quest, use PlaylistManager’s **Refresh Playlists** to load the generated lists in the song browser.

## Playlists

| Playlist | What it does |
| --- | --- |
| **PP Gain / AP Gain** | By default, each enabled leaderboard gets one combined playlist of unplayed charts and improvements. It ranks by estimated weighted profile gain and keeps the strongest recommendation per song. AccSaber calls this playlist **AP Gain**. |
| **Not Played / To Improve** | Turn on **Separate New and Repeat Tracks** to split new charts and potential personal-best improvements into two playlists. |
| **BeatLeader Maps to Conquer** | Finds maps where your predicted score can capture territory or reduce a clan PP gap. Choose your main clan or one playlist per joined clan. Secondary clans are labeled **what-if primary clan**. **Legacy Clan Playlists** switches to closest-gap ordering with an automatic star cap. [Clan recommendation rules](https://github.com/Kaiiak04/RankedTools/blob/v0.1.31/docs/CLAN_RECOMMENDATIONS.md) |
| **Sync** | With at least two accounts selected, enable **Create Sync playlist** to find charts with a posted score on one selected leaderboard and a missing score on another. Sync includes every match across all characteristics, even if personal playlists are disabled. |

Sync matches the exact song hash, characteristic, and difficulty. **Required Sync Scoreboards** sets how many selected services must rank the chart, with a default of 2 and a maximum of 3. Posted No Fail scores count for Sync. Sync uses the full ranked catalogs at any track limit, clear rate, or AccSaber category setting.

## Recommendation rules

- BeatLeader personal and clan playlists include ranked **Standard and OneSaber** automatically. Standard and OneSaber share the same accuracy and clearability models. Turn on **BeatLeader Modifiers** to compare ranked speed options and compatible scoring modifiers. **Song details** shows the recommended modifiers and effective stars.
- The accuracy model weights recent scores more heavily using a 30-day exponential decay time constant. BeatLeader and ScoreSaber fit accuracy against effective stars. AccSaber uses separate fits against Complexity for each category. [Full recommendation rules](https://github.com/Kaiiak04/RankedTools/blob/v0.1.31/README.md#recommendation-rules) · [BeatLeader PPv4.1 calculations](https://github.com/Kaiiak04/RankedTools/blob/v0.1.31/docs/PPV4.md)
- **Min Clear Rate** defaults to 50% and can be set from 0% to 95% in five-point steps. It filters unplayed personal charts, harder BeatLeader modifier replays, and personalized clan recommendations. Clearability is an estimate based on nearby scores and attempts.
- **Record local attempts** and **Import attempt history** are enabled by default. Use your own accounts while recording. To import public BeatLeader attempt history, enable public statistics on your BeatLeader profile. Cached and local attempts stay usable if an import fails.

RankedTools writes `.bplist` files with stable `RankedPractice_` names. A failed or incomplete refresh preserves the previous affected playlist. Downloaded maps and unrelated playlists are preserved. Generated playlists use hand-drawn covers, and Maps to Conquer uses your clan’s icon when available. [Artwork credits](https://github.com/Kaiiak04/RankedTools/blob/v0.1.31/assets/ARTWORK.md)

## Build from source

Check out the `v0.1.31` tag to build the version documented here. [Release source](https://github.com/Kaiiak04/RankedTools/tree/v0.1.31)

- On **Quest**, configure qpm-rust, PowerShell 7, CMake, Ninja, and the Android NDK. Run `qpm s qmod` from the repository root. [Regression checks](https://github.com/Kaiiak04/RankedTools/blob/v0.1.31/tests/README.md)
- For **PCVR**, use the Windows build and packaging scripts under `pcvr/` with the target game version. [PCVR build instructions](https://github.com/Kaiiak04/RankedTools/blob/v0.1.31/pcvr/README.md#build)

RankedTools is licensed under **GPL-3.0-only**. The project started from Lauriethefish’s Quest mod template, which is available under the Unlicense. [License](LICENSE) · [Third-party notices](https://github.com/Kaiiak04/RankedTools/blob/v0.1.31/THIRD_PARTY_NOTICES.md) · [Quest mod template](https://github.com/Lauriethefish/quest-mod-template)
