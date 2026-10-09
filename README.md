# RankedTools

RankedTools is a Quest Beat Saber mod that creates personalized BeatLeader and ScoreSaber playlists to help you gain PP, improve existing scores, and discover ranked maps suited to your ability.

**Current version: [0.1.22 source preview](https://github.com/Kaiiak04/RankedTools/releases/tag/v0.1.22), for Quest Beat Saber 1.40.8_7379.** The latest account picker, signed-in BeatLeader integration, normal loading phase and refresh optimizations still need headset testing. No downloadable QMOD is published yet: the current prebuilt curl dependency contains OpenSSL 1.1.1 and needs GPL-compatible TLS licensing before binary distribution. See [third-party notices](THIRD_PARTY_NOTICES.md).

Recent updates: [0.1.22](CHANGES-0.1.22.md) reuses HTTPS connections, refreshes the leaderboards concurrently, improves ScoreSaber API compatibility and displays elapsed refresh time. [0.1.21](CHANGES-0.1.21.md) moves RankedTools from Early Mod to normal Scotland2 loading. The Quest build and 2,174 automated checks passed locally; actual refresh times still need measurement on a headset.

For each enabled leaderboard, the mod normally writes two PlaylistManager playlists:

- `<Leaderboard> - Not Played`
- `<Leaderboard> - To Improve`

Enable **Simple Mode** to replace these with one `<Leaderboard> - PP Gain` playlist per leaderboard. It combines unplayed charts and improvements, ranks by theoretical weighted profile PP gain, and applies the configured track limit to the merged playlist. The highest-gain chart is kept when a song appears in both sources. Simple Mode is off by default.

When the BeatLeader clan playlist is enabled, it writes `BeatLeader - Maps to Conquer (<clan tag>)` for the first clan in the account's BeatLeader clan order by default. With all-clans enabled, it creates one separately ranked playlist per joined clan, each using the configured track limit.

## In-game flow

1. Install this mod alongside PlaylistManager and the leaderboard services you use.
2. In **Mod Settings**, select your BeatLeader and/or ScoreSaber account using **Choose account** → **Find account** → **Select**. Enter a display name, a profile URL, or a numeric ID. Search results show the account name, avatar when available, country, rank, and ID; use the page buttons for more matches. ScoreSaber name searches require at least three characters. Alternatively, press **Use signed-in BeatLeader account** when signed into a compatible BeatLeader mod. Your selection is saved and shown by name; **Show / hide ID** reveals the stored ID. Existing saved IDs remain usable. **Clear saved account** removes the selection without deleting attempt history.
   Enable the desired playlists, set the track limit, and adjust **Min Clear Rate**. Its slider ranges from 0% to 95% in 5% steps, defaults to 50%, and replaces the old preset modes. Enable **Simple Mode** for one combined playlist per leaderboard. For the clan playlist, choose automatic stars (matching the BeatLeader unplayed-chart star band) or turn Auto off and set the inclusive manual minimum and maximum. Choose main clan or all clans.
3. Open the **RankedTools** tab from the main menu to refresh recommendations. Use **Song details** and its playlist/song buttons to see why a map was selected. Choose **Ability chart**, then **BeatLeader** or **ScoreSaber**, to see your eligible best scores and the accuracy curve used for recommendations.
4. Use PlaylistManager's **Refresh Playlists** action to reload the generated playlists in the song browser. PlaylistManager can download missing maps from those playlists.

The mod calls the public leaderboard APIs directly. The signed-in BeatLeader button reads only the numeric player ID through BeatLeader's optional public `LoggedInPlayerId` API, then verifies the public profile before saving it. It does not read authentication tokens or depend on private hooks. BeatLeader is optional: if the mod/API is unavailable or you are logged out, use account search instead. ScoreSaber account selection uses its public API. Name searches and profile links require an explicit **Select** action; no first-match selection occurs. Aliases and linked BeatLeader IDs are resolved to the primary numeric ID before saving, keeping new local attempt records associated with that profile. API errors preserve your previous selection; obsolete search replies cannot replace newer results. Opening settings refreshes saved profile names without switching accounts automatically.

**Record local attempts** and **Import attempt history** are enabled by default. Local attempts are associated with the player IDs configured when gameplay starts: use your own IDs, and turn recording off when inspecting someone else's profile or watching replay playback. The output files are written to PlaylistManager's Quest playlist folder with stable `RankedPractice_` filenames so upgrades replace earlier generated playlists instead of creating duplicates. Refreshing replaces only the generated playlist files. It does not delete downloaded songs or modify other playlists.

## Recommendation rules

- **Not Played** fits the player's recent accuracy-by-stars curve, then estimates the weighted profile PP gain of a new score. Recorded clears and actual failures inform clearability, including clears below a personal best. Leaderboard best scores remain fallback evidence. No Fail scores provide weaker evidence only on charts without a normal clear or usable actual attempts. A failed run does not mark a chart as played. The selected star band and eligible charts depend on Min Clear Rate. Played hash/characteristic/difficulty combinations are excluded. Rows without a valid 40-character BeatSaver hash are excluded so PlaylistCore can load the whole playlist.
- **Min Clear Rate** requires an unplayed chart's rough clear estimate to meet the selected percentage. At 0%, charts are ranked by weighted PP gain alone, matching the old Max PP setting. Above 0%, the normal Not Played playlist ranks eligible charts by weighted PP gain multiplied by the clear estimate. At 95%, the search moves toward easier charts when the observed star range is too risky. The estimate is a heuristic based on nearby scores and is not a measured probability; song style and practice can change actual clearability.
- **Simple Mode** keeps the same minimum clear estimate for unplayed charts, then ranks the combined pool by weighted PP gain alone. Source lists are evaluated before applying the shared limit, so chance-adjusted ranking cannot discard a higher-gain chart first. Improvements use the existing score as the PP baseline; unplayed charts use an inserted score. Clan playlists stay separate.
- The in-game recommendation view shows the selected difficulty, predicted accuracy and PP, estimated weighted PP gain, confidence based on nearby history, and nearby clears, failures, and No Fail runs. Unplayed recommendations show the rough clear estimate; improvements show current PP. The preview only cycles through enabled playlists in the selected layout.
- **Ability chart** plots accuracy (%) against effective stars for each enabled leaderboard. Gray dots are the same eligible ranked Standard best scores used for fitting (older scores are dimmer); cyan is the recency-weighted monotonic fit. Shaded regions and dashed curve sections mark estimates outside the observed star range. Exact fitted knots and tail boundaries are plotted, including the endpoint hold and cautious extrapolation. This estimates accuracy on a scored run, not clear probability or map-style-specific ability. No Fail runs and sub-55% scores are excluded. Empty histories show an explanation instead of presenting the generic 85% fallback as a personal fit. Charts reuse the profile history fetched for playlists and are independent of the track limit and Simple Mode. Scroll within the panel if the graph or explanation extends below the viewport.
- Preview text wraps within the panel and grows vertically with its contents. Playlist and song navigation each use a pair of side-by-side buttons. If BeatLeader denies attempt-history access with HTTP 401, the preview explains that public statistics must be enabled on the BeatLeader profile to import past attempts. Cached/local attempts remain usable and playlist generation continues. Account privacy settings are not changed by the mod.
- **To Improve** uses the same recency-weighted monotonic accuracy fit as Not Played. Conflicting observations are pooled into weighted averages, and predictions interpolate between fitted star ratings. Increasing stars can only maintain or reduce predicted accuracy. Each chart is evaluated at the fitted accuracy, and recommendations are ranked by the simulated increase to weighted profile PP if that score replaced the current best.
- Scores marked with No Fail (`NF`) are ignored for played-map detection, accuracy fitting, target-star estimation, and To Improve. They provide weaker evidence for the clearability estimate. Other modifier scores still count as played and retain the leaderboard's PP. Recommendation candidates estimate an unmodified play. BeatLeader observations use `modifiedStars` when returned, or derive effective stars from the chart's speed ratings and modifier values. ScoreSaber's ISO score dates are parsed in UTC for recency weighting.
- The weighted PP simulation includes ranked scores across all characteristics, including OneSaber, even though recommendation candidates are Standard charts.
- ScoreSaber predictions use its current accuracy curve and star multiplier. BeatLeader predictions estimate an unmodified play using the map's acc/pass/tech ratings and its public PP calculation. Predicted PP is conditional on scoring the displayed predicted accuracy.
- The configured number of tracks is capped at 100 per playlist.
- **BeatLeader Maps to Conquer** uses each clan maps endpoint's `toconquer` ordering (closest weighted PP remaining first) and filters to Standard charts and the configured inclusive star range. All-clans mode creates separate playlists per clan, preserving each clan's own order and results.
- Refresh removes obsolete generated clan playlists when clan membership or the all-clans setting changes; it only removes files using this mod's clan playlist filename prefix.
- Auto stars uses the same score history, target-star estimate, and ±1.5★ window as BeatLeader **Not Played**. It is enabled by default; switching it off applies the manual min/max values.

Generated leaderboard playlists use hand-drawn developer covers for Not Played, To Improve, and PP Gain / Simple Mode, supplied by me!!! :]
**Maps to Conquer uses the clan's own icon from BeatLeader**, including a distinct icon for every playlist in all-clans mode. Clan metadata comes from the existing map response; its PNG/JPEG icon is fetched and embedded into that playlist's `.bplist` image field. Missing icons, failed downloads, and unsupported images fall back to the supplied clan cover, with a warning, without blocking playlist generation. Refreshing retrieves the current icon again. Bundled artwork details are recorded in [assets/ARTWORK.md](assets/ARTWORK.md).

Both services load the complete best-score history returned by their paginated public APIs and compare all ranked candidates in the selected star band before limiting the playlist to distinct songs. ScoreSaber map queries use its v2 endpoint with 100 charts per page and server-side star filters. A refresh reuses each service's score and attempt history across its playlists. Large histories or star bands can make refreshes take longer. A failed or incomplete score/candidate response preserves the affected playlist. Attempt-history failures instead report a warning and use cached/local evidence, so unavailable attempt statistics do not block playlists.

Each refresh captures the settings when it starts. Changes made while it runs take effect on the next refresh. A successful clan membership response with no joined clans removes obsolete generated clan playlists; a membership API error preserves them.

Refreshes reuse HTTPS connections between requests and run ScoreSaber alongside BeatLeader. BeatLeader and its clan playlists share one worker and profile history. Scores and the complete candidate star band are still fetched afresh; recommendations use the same ranking rules. Requests within each service remain sequential with the existing pacing. The completion message shows total elapsed time, and the mod log records each stage's time, request count and new connections. Large profiles, first-time attempt imports and slow APIs can still take several seconds.

Switching Simple Mode takes effect after refreshing recommendations and reloading PlaylistManager. Replacement playlists are fetched and saved before the previous layout is removed. Cleanup only targets this mod's three exact leaderboard playlist filenames per service. Disabled services are not fetched, but files from their incompatible layout are removed on refresh too. Other playlists, clan playlists and downloaded songs are preserved.

## Build

The monotonic accuracy model is documented in [CHANGES-0.1.15.md](CHANGES-0.1.15.md). Attempt logging, import limits, deduplication, clearability model details, and storage are documented in [CHANGES-0.1.14.md](CHANGES-0.1.14.md).

This is a Quest C++ QPM project. With qpm-rust, CMake, Ninja, and the Android NDK configured, build and package it with:

```powershell
qpm s qmod
```

The QMod manifest currently targets Beat Saber `1.40.8_7379`. Confirm the modding tool you use offers that build before installing; Quest mod compatibility depends on the exact game build and the current community mod catalog.

Regression checks and their runner are documented in [tests/README.md](tests/README.md). The original review and resolved findings are in [CODE_REVIEW.md](CODE_REVIEW.md) and [FIXES.md](FIXES.md).

## Current limits

- The in-game refresh is triggered by opening the RankedTools tab; PlaylistManager must be refreshed afterward to reload generated playlists.
- Recommendations use a fitted performance curve, a heuristic clear estimate, and a weighted-profile PP simulation. Estimates can be imperfect when score history is sparse or chart ratings are missing. A No Fail run shows that a chart was attempted; it does not prove the player cannot clear it.
- PlaylistManager must be installed separately. The mod intentionally does not manage or delete song files.

RankedTools is licensed under **GPL-3.0-only**; see [LICENSE](LICENSE). Third-party code retains its own terms, documented in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The project started from [Lauriethefish's Quest mod template](https://github.com/Lauriethefish/quest-mod-template), which is available under the Unlicense.
