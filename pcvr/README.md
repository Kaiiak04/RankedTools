# RankedTools PCVR preview

RankedTools creates personalized BeatLeader and ScoreSaber playlists to help you gain PP, improve existing scores, and discover ranked maps suited to your ability. This is the Windows x64 PCVR port of the Quest mod.

**Preview version: 0.1.26-pcvr.3. Targets: Beat Saber 1.40.8 and 1.44.1. In-game validation is pending on both.** Use the ZIP matching your game version. These packages include both the C# plugin and its native recommendation engine.

## Install

1. Install the core mods for your game version using BSManager: **BSIPA, BeatSaberMarkupLanguage, SiraUtil, SongCore, and BeatSaberPlaylistsLib**, including their dependencies.
2. Close Beat Saber. Open the matching RankedTools ZIP, select everything inside it (Ctrl+A), and copy it (Ctrl+C). Open that version's game folder—the folder containing **Beat Saber.exe**—and paste (Ctrl+V). Windows combines the `Plugins`, `Libs`, and `UserData` folders automatically. Allow replacement of existing RankedTools files when updating. You should have `Plugins/RankedTools.dll` and `Libs/Native/RankedTools.Core.dll` directly beneath the game folder, along with the supplied `UserData/RankedTools/` assets and CA bundle.
3. Install a compatible playlist browser/downloader if you want to browse the generated playlists and download missing maps. For **1.40.8**, PlaylistManager 1.7.3 is available through its verified mod list. For **1.44.1**, PlaylistManager is absent from the verified list checked for this preview; the [community PlaylistManager 1.7.4 build for the 1.42 API family](https://github.com/iza-ttv/PlaylistManager/releases/tag/v1.7.4) is a tester option, with actual 1.44.1 compatibility still pending. RankedTools can generate `.bplist` files independently of that browser. Exact optional download URLs and SHA-256 hashes are in `optional-dependencies.lock.json`.
4. Start Beat Saber. Open **Mod Settings → RankedTools**, choose your accounts, then open **RankedTools** from the main menu to refresh recommendations.

BeatLeader and ScoreSaber's in-game mods are optional. Recommendations use their public APIs directly. **Use current PC account** reads your Steam/Oculus PC user ID, verifies the corresponding public profile, and saves that selection. Account search also accepts display names, profile URLs, or numeric IDs, with explicit selection and page buttons. It never reads login tokens. Your PC platform account may differ from a manually selected leaderboard account, so check the displayed profile.

## Use

Enable **Create Sync playlist** after selecting both accounts to fill missing scores between leaderboards. Sync matches exact hash, characteristic and difficulty, requires ranked status on both services, and includes all charts scored on exactly one. All characteristics and all matches are included, regardless of song count, Simple Mode, Min Clear Rate or personal playlist toggles. Posted No Fail scores count as filled; local attempts do not. Multiple qualifying charts are grouped under the song, with each missing leaderboard shown in the preview. Complete catalogs can take longer to fetch. A failed or incomplete fetch/save preserves the prior playlist; a successful empty result writes an empty playlist.

- The normal layout creates **Not Played** and **To Improve** playlists for each enabled leaderboard. **Simple Mode** replaces those with one **PP Gain** playlist per leaderboard.
- **Min Clear Rate** defaults to 50% and ranges from 0–95% in 5% steps. At 0%, the threshold and chance adjustment are disabled. Clearability is a heuristic, not a measured probability.
- Clan playlists include personalized capture/progress suggestions, optional legacy ordering, main/all-clan selection, clan artwork, and capture targets. Only your primary clan receives actual contributions; secondary-clan results are labeled what-if estimates.
- **Song details** has playlist and song navigation, predictions, weighted gains, confidence, and nearby attempt evidence. **Ability chart** shows the same fitted model used by recommendations, including its observed range and extrapolation.
- Local solo attempts and available public attempt imports are enabled by default. Configure your own IDs when recording. Practice, quit, restart, failure, modifier, and replay handling retains the existing recommendation semantics. Recognized BeatLeader and ScoreSaber replay sessions are excluded; if a loaded leaderboard mod's replay state cannot be verified, local recording skips that session and logs a warning. Public imports remain usable.
- Refresh captures settings when it starts. Changes take effect on the next refresh. Saved playlists retain the stable `RankedPractice_` filenames. API or save failures preserve previous affected output; cleanup only targets this mod's generated files.

The PC playlist directory comes from BeatSaberPlaylistsLib's default manager, normally `Playlists` in your game folder. RankedTools requests a browser reload after changes. Downloaded songs and other playlists are preserved.

Settings are saved by BSIPA in `UserData/RankedTools.json`. Attempt history, clan caches, artwork, and the HTTPS CA bundle live in `UserData/RankedTools/`. The preview does not import settings or history from a Quest installation. To uninstall, remove `Plugins/RankedTools.dll` and `Libs/Native/RankedTools.Core.dll` while the game is closed. Settings, history, and generated playlists can be kept for a later reinstall.

## Compatibility and validation

| Package | Compilation references | Legato profile | In-game check |
| --- | --- | --- | --- |
| 1.40.8 | Exact public 1.40.8 references | 1.40.0 | Pending |
| 1.44.1 | Public 1.42.0 references | 1.42.0 | Pending |

The public stripped-reference repository currently stops at 1.42.0. [Legato](https://github.com/ScoreSaber/legato) groups 1.42.0–1.44.1 into one compatibility profile. The 1.44.1 ZIP is an explicitly targeted preview built with that profile; compilation does not establish actual 1.44.1 behavior.

Automated checks cover the shared production models and APIs, Windows storage and failure preservation, native ABI and chart transfer, managed attempt/account state, both menus' bindings, and real HTTPS through the packaged engine. `validation.json` records the checks for the exact plugin and engine hashes. See [TESTING.md](TESTING.md) for your in-game checklist. Windows x64 is the initial platform; Linux/Proton and ARM64 are not covered.

## Build

Use .NET SDK 10, PowerShell 7, CMake 3.22+, Git, and Visual Studio 2022's C++ tools. Build reference assemblies are compile-only and cannot run Beat Saber. From the repository root:

```powershell
pwsh ./pcvr/build.ps1 -GameVersion 1.40.8
pwsh ./pcvr/build.ps1 -GameVersion 1.44.1
pwsh ./pcvr/test.ps1 -LiveHttps
pwsh ./pcvr/package.ps1 -GameVersion 1.40.8
pwsh ./pcvr/package.ps1 -GameVersion 1.44.1
```

The restore step uses the game-reference commits in `targets.json`, archive hashes in `dependencies.lock.json`, and NuGet lockfiles. Release ZIPs contain RankedTools, artwork, certificates, and notices; game references and other mods are not bundled. A portable llvm-mingw toolchain can be supplied with `-LlvmMingw <directory>` to build and package locally without MSVC; use a separate native build directory when switching compiler families.

Quest remains the root QPM build. The shared C++ engine is compiled for both platforms; PC menus and hooks live under `pcvr/`. See the root [recommendation documentation](https://github.com/Kaiiak04/RankedTools/blob/main/README.md) for the detailed ranking rules.
