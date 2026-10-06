RankedTools code review

**Status: all eight findings below were fixed in 0.1.12.** See [FIXES.md](FIXES.md) for the implemented changes and [tests/README.md](tests/README.md) for regression checks. The findings, original line references, and review evidence below describe the pre-fix source.

The original review found that the core unmodified PP formulas agree with the public calculations, with eight actionable issues around candidate selection, profile data, refresh state, and clan cleanup. These explained why a playlist could look sensible in-game while its ordering, track count, or displayed gains were wrong.

1. **[P1] Not Played stops fetching before it knows whether the candidates can actually be recommended.**

   Locations: [BeatLeader fetch loop](C:/Users/kaiwo/Documents/BeatLeaderPP/src/recommendation_api.cpp:461), [ScoreSaber fetch loop](C:/Users/kaiwo/Documents/BeatLeaderPP/src/recommendation_api.cpp:537), [recursive parser](C:/Users/kaiwo/Documents/BeatLeaderPP/src/recommendation_api.cpp:317).

   Both fetchers stop at `limit * 8` parsed rows. Clearability, positive-gain checks, valid-hash checks, final ranking, and deduplication happen afterward. The parser emits repeated entries when visiting a chart and its nested metadata: the live BeatLeader map sample produced 603 entries for 201 unique charts on 100 songs. ScoreSaber produced two entries per chart.

   A native fixture with 11 songs and four charts each produced 132 parsed entries, exceeding the default 128-row stopping threshold. Final selection contained only 11 songs for a requested 16. An upper-star-band fixture passed this same stopping condition but returned zero recommendations in Stretch mode, while an eligible lower-star-band chart existed. BeatLeader sorts the API pages by descending stars, so this can prevent it from ever reaching easier, clearable candidates. Even when the playlist fills, a later candidate with a greater predicted gain can be omitted.

   Applies to both services' Not Played lists; particularly noticeable with multiple ranked difficulties, sparse history, and Comfortable/Stretch filtering. Count distinct eligible songs after selection, continue fetching when the result underfills, and compare a deliberately chosen candidate pool before claiming the best gains.

2. **[P2] Weighted profile gains omit ranked non-Standard scores.**

   Location: [MakeWeightedProfile](C:/Users/kaiwo/Documents/BeatLeaderPP/src/recommendation.cpp:333).

   Filtering recommendation candidates to Standard is a valid design choice. Filtering the existing PP profile to Standard changes the simulated weights, because ranked OneSaber scores also occupy positions in the real weighted profile. BeatLeader's [profile calculation](https://github.com/BeatLeader/beatleader-server/blob/master/Utils/PlayerUtils.cs) sorts eligible ranked scores without a Standard-only restriction.

   Reproduction: a player with a 500-PP OneSaber score and a 100-PP Standard score gains 286.1225 profile PP by adding a 300-PP Standard score. This code predicts 296.5. The error grows when more excluded scores sit above the candidate. To Improve can also be misordered because the affected charts occupy different positions in the complete profile.

   Applies to accounts with PP-contributing alternate-characteristic scores. Keep those scores in the weighted simulation while restricting the recommendations and accuracy training separately.

3. **[P2] ScoreSaber's advertised recency weighting is absent.**

   Locations: [timestamp parsing](C:/Users/kaiwo/Documents/BeatLeaderPP/src/recommendation_api.cpp:262), [observation weighting](C:/Users/kaiwo/Documents/BeatLeaderPP/src/recommendation.cpp:37).

   The parser reads numeric `timepost` and `timeset`. ScoreSaber returns an ISO date string under `timeSet`; neither the field name nor format is handled. All 100 distinct live score charts tested ended up with timestamp zero and the same fallback weight of 0.65.

   The README says To Improve fits a recency-weighted curve, but an old best score and a recent best score receive equal age weight on ScoreSaber. This also affects Not Played's curve, target stars, clearability, and confidence. It matters when a player's skill has changed since older scores. Parse the ISO `timeSet` value into a Unix timestamp. See the [ScoreSaber API schema](https://scoresaber.com/api/openapi.json).

4. **[P2] BeatLeader speed-modifier scores train the model at ordinary stars.**

   Locations: [modifiedStars parsing](C:/Users/kaiwo/Documents/BeatLeaderPP/src/recommendation_api.cpp:249), [accuracy-model fallback](C:/Users/kaiwo/Documents/BeatLeaderPP/src/recommendation.cpp:83), [clearability-model fallback](C:/Users/kaiwo/Documents/BeatLeaderPP/src/recommendation.cpp:192).

   The code expects the score-history API to provide `modifiedStars`, but the current response omits that property. All 100 distinct live charts tested used the ordinary-star fallback. One actual FS score had ordinary stars 9.005639 and FS stars 10.393583; the model associated its accuracy and successful clear with 9.005639 stars.

   FS/SF scores can make the player look weaker than they are at ordinary speed. Slower Song scores can make the player look stronger and more capable of clearing ordinary-speed charts than the evidence supports. This affects both personal BeatLeader playlists and the clan playlist's automatic star range. Retain the score's modifiers and derive its effective rating from the supplied modifier ratings and values, matching [BeatLeader's calculation](https://github.com/BeatLeader/beatleader-server/blob/master/Utils/ReplayUtils.cs). The [score response projection](https://github.com/BeatLeader/beatleader-server/blob/master/Controllers/PlayerScoresController.cs) explains why merely reading `modifiedStars` does not work.

5. **[P2] ScoreSaber's ranked-map scan covers at most 420 charts, despite requesting 100 per page.**

   Location: [ScoreSaber ranked-map request](C:/Users/kaiwo/Documents/BeatLeaderPP/src/recommendation_api.cpp:519).

   The legacy leaderboard-list endpoint does not support `limit`. Live requests with both `limit=1` and `limit=100` returned 14 charts; metadata on page 30 also reported 14 per page. Consequently, the 30-page budget scans at most 420 ranked charts, or fewer when the early stopping condition fires. The live endpoint reported 6,187 ranked charts total.

   The README acknowledges a bounded scan, so boundedness itself is documented. The unsupported parameter makes the actual pool substantially smaller than the request suggests, and filtering to a player's star band shrinks it further. Strong candidates outside those first pages never compete. Use a supported endpoint with an effective limit and server-side star filtering, or explicitly size pagination around the legacy endpoint's 14-row pages. See the [official API schema](https://scoresaber.com/api/openapi.json).

6. **[P2] BeatLeader score history can be incomplete, too, and a filtered-empty page is mistaken for the end.**

   Locations: [history budget and stopping condition](C:/Users/kaiwo/Documents/BeatLeaderPP/src/recommendation_api.cpp:364), [score acceptance](C:/Users/kaiwo/Documents/BeatLeaderPP/src/recommendation_api.cpp:269).

   BeatLeader history stops after 50 pages of 100 API scores. Like the ScoreSaber limitation called out in the README, this can cause already-played BeatLeader charts beyond the budget to appear in Not Played and omit data from model fitting.

   There is also an earlier exit: the fetcher stops whenever a page adds no *parsed* entries. A nonempty page of unrated songs is discarded by the `stars <= 0` check and triggers that exit even if more API pages remain. NF runs have zero PP and are sorted toward the bottom, so a page of unrated zero-PP plays preceding ranked NF plays can prevent those attempts from reaching the clearability model. The native fixture reproduced the nonempty-page-to-zero-entries condition; an account exhibiting that exact page ordering was not required or assumed.

   Applies to large histories or histories with unrated pages before useful later rows. Detect the end from the response's raw rows and pagination metadata, and make any remaining history budget explicit in the UI/documentation.

7. **[P2] Leaving the final clan does not remove obsolete clan playlists.**

   Locations: [no-clans return](C:/Users/kaiwo/Documents/BeatLeaderPP/src/recommendation_api.cpp:592), [cleanup after successful fetch](C:/Users/kaiwo/Documents/BeatLeaderPP/src/main.cpp:101).

   The README says membership changes remove obsolete generated clan playlists. If the profile has no clan tags, the fetch returns an error. Refresh returns before cleanup, so playlists from the previous membership remain on disk. A verified empty membership should be a successful empty result followed by pruning; a network failure should still preserve existing playlists.

   This was established by control-flow review, without changing clan membership or headset files. Separately, disabling a playlist type only stops its refresh; existing generated files are retained.

8. **[P2] Editing settings during refresh can race with the worker's configuration reads.**

   Locations: [worker configuration reads](C:/Users/kaiwo/Documents/BeatLeaderPP/src/main.cpp:111), [detached worker](C:/Users/kaiwo/Documents/BeatLeaderPP/src/main.cpp:162), [configuration accessor](C:/Users/kaiwo/Documents/BeatLeaderPP/extern/includes/config-utils/shared/config-utils.hpp:102).

   The worker repeatedly reads shared configuration while the settings UI can write it. The bundled config-utils accessor does not lock those reads or the value assignment; its mutex only protects change-event handling. Concurrent access to the underlying JSON/string storage is a C++ data race. A refresh can also mix settings from different moments, for example using one player's personal star range with a subsequently changed player ID for clan playlists.

   Applies when settings are edited while a refresh runs. Snapshot the complete refresh configuration on the main thread and pass an immutable copy to the worker. This is a static concurrency finding; no crash was reproduced.

Validation: I compiled an isolated C++ harness that directly includes the unmodified production recommendation and API-parser source, then executed it against public API samples and targeted fixtures. Both PP curves checked out: ScoreSaber's multiplier table agreed within about one millionth, and 80 unmodified BeatLeader scores agreed within about one millionth relative error. NF detection/exclusion also passed the tested cases. The clan `toconquer` descending signed-PP ordering agrees with the current server implementation. No Quest gameplay, Unity UI, or headset filesystem test was performed. The harness and required API samples are retained in `.review/` for follow-up verification.
