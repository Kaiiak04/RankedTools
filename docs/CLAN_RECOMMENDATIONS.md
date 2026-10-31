# Personalized clan recommendations

Personalized clan playlists are the default and estimate what an additional unmodified play could achieve for a clan. They use the existing personal accuracy-by-stars fit, chart-specific clearability evidence, and the chart's PPv3 or PPv4 formula. They do not apply the personal-profile PP-gain filter: an easier chart can be useful for conquest even when it adds almost nothing to personal profile PP.

## Optional legacy mode

The single **Legacy Clan Playlists** toggle switches the existing clan playlists to legacy mode on the next refresh. It defaults to off. The old Auto, minimum-star and maximum-star controls remain removed. Playlist filenames, main/all-clan behavior, limits and covers are shared by both modes; switching modes replaces the same generated files.

Legacy mode fetches the complete paginated clan map list in BeatLeader's `toconquer` descending order, using its signed weighted clan gap. It filters Standard charts only by an automatic upper star limit derived directly from the global clearability model and Min Clear Rate. The cap searches for the highest qualifying star region, then refines its upper boundary. Its search starts beyond an evidence-derived tail bound and handles multiple qualifying regions. It does not reuse personal PP optimization or add the old 1.5-star search-window padding. There is no minimum star limit, manual cap, or separate per-chart clearability threshold. At Min Clear Rate 0%, the upper cap is disabled. Displayed finite caps round down to two decimals. The fitted accuracy and each chart's PPv3/PPv4 formula estimate unmodified personal PP. An eligible personal PB at or above that estimate removes the chart, including a higher PB earned with modifiers; No Fail is not an eligible PB. Filtering precedes best-chart-per-song selection and the track limit, so another difficulty or a later page can fill a vacated slot.

This mode retains the legacy candidate pool: maps represented in that clan's map list. Current clan gap determines order; personal PP is used for the improvement filter rather than estimating weighted clan benefit. Secondary clans are labeled **what-if primary clan**, and their legacy order reflects current totals before any PB transfer. The preview shows the automatic maximum, current/predicted personal PP, clear estimate, support, and current clan gap. The signed gap is kept in the ranking field, separate from personal PP.

Legacy mode shares the refresh worker's membership and score/attempt history, uses 500-row pages and request pacing, and checks pagination and repeated charts. Failed/incomplete clan pages preserve that clan's prior playlist independently; genuinely empty filtered results can replace it. It bypasses catalog/global-ownership downloads and per-map capture verification. Personalized exploration and learned search history are left untouched, ready for switching back. Upstream legacy ordering is defined in [ClanControllerHelper](https://github.com/BeatLeader/beatleader-server/blob/master/ControllerHelpers/ClanControllerHelper.cs).

## Personalized contribution and ranking

BeatLeader's map-level clan score weights eligible General-context contributions by `0.8^position`, starting at position zero. The mod mirrors the server's ordering: PP rounded to two decimals, accuracy rounded to four, then score time. A projection replaces the player's existing contribution or inserts a new one and recalculates every affected weight. Actual modifier PBs retain their published PP; projected plays are unmodified. Server totals anchor the calculation, with only the recomputed difference added to them.

The capture target is the strongest rival's map PP plus `0.01`. The remaining gap includes this margin, so ties are not presented as captures. Maps already held are excluded. The ordering is:

1. Capture at the predicted score.
2. `chance × min(1, projected clan gain / remaining gap)`.
3. `chance × projected clan gain`.
4. Supporting history, song name, hash, and leaderboard ID.

Here `chance` is the existing rough clear estimate. At Min Clear Rate 0%, its ranking multiplier becomes one and the clear threshold is disabled. Otherwise every candidate, including replays, must meet the selected threshold. Positive non-capture progress must add at least 1 weighted clan PP. A smaller contribution can qualify if it captures the map. Charts are compared before choosing the best chart per song and applying the track limit.

A replay must exceed the player's existing eligible PB. No automatic improvement bonus is invented. Playing at or above the predicted score removes that replay until the fitted skill estimate supports a higher target. A prediction can remain optimistic about a particular chart: the accuracy model is based on stars rather than map-style-specific skill. Confidence describes nearby supporting evidence and is not a calibrated probability.

The displayed capture accuracy is solved using the same weighted contribution calculation and the chart's PP formula, with other scores held fixed. If even 100% unmodified accuracy is insufficient, the preview says so. A capture at the predicted score does not guarantee the player will achieve that score or that rivals will remain unchanged.

## Personalized search coverage and failure handling

Every refresh loads the player's score/attempt history and the bulk global clan snapshot. Complete ranked Standard catalog and clan map totals are loaded on a cache miss: catalog data is reusable for 10 minutes, and per-clan totals for 1 minute. Bulk requests use 500 rows per page. Clan selection always permits every star rating and uses skill and clearability to select suitable charts. The former Auto toggle and manual min/max star controls have been removed; saved values for them are ignored. Profiles without an eligible training score receive an explanatory error and preserve previous playlists.

Bulk data is a screening aid. It is not proof of current ownership or a zero-PP opponent. Each shortlisted chart requires fresh top-three clan rankings and complete associated scores for the target clan. Secondary-clan calculations also fetch the current primary clan's scores. Top-three rankings suffice to retain the strongest unaffected rival when both target and primary clans must be excluded. Responses are shared within the refresh across clans; no old verified score lists are reused across refreshes. A verified chart's current ratings, PP formula marker, ranked status and hash are read from its live leaderboard response, and performance is reevaluated before it can be selected. New maps and reweights may take up to 10 minutes to affect screening; cached metadata cannot establish a final capture.

The per-clan verification budget is `clamp(4 × track limit, 64, 256)`. Previously selected charts that still meet the skill/PB filters are reserved for fresh verification, using at most half the budget. Half the remaining slots target estimated captures, a quarter targets the best remaining gap closure, and a quarter rotates through the remaining charts in leaderboard-ID order. Unused slots spill into the ranked pool. At the default 16-track limit, 16 previous picks leave 48 slots: 24 capture candidates, 12 progress candidates and 12 exploration candidates. The first search retains the original 32/16/16 split.

Search history under `RankedPractice/clan-search` remembers selected chart IDs and compact weighted-impact summaries from successful verification, including checks that found no useful contribution. Matching PBs, predictions and bulk totals allow those summaries to guide screening: high personal PP with negligible clan benefit no longer repeatedly crowds out useful alternatives. Changed inputs invalidate the hint. Prior score lists are never persisted or used for final verification. Search history is isolated per player/clan and resets its hints after a primary-clan change; legacy cursor files retain their exploration position. It is saved after the playlist is successfully written. A transient failed check also retains that chart's retry priority.

Exploration discovers alternatives to compete against the freshly rechecked recommendations. Good picks no longer disappear merely because exploration moved on. An unchanged snapshot can still produce a better playlist as stronger opportunities are discovered; selected tracks are never pinned against better verified results. The preview reports checked/eligible charts, previous picks rechecked, candidates using learned impact, and verification failures. This bounded search can miss better opportunities, especially on its first refresh.

Missing essential inputs preserve previous output. Malformed or incomplete contribution pages, repeated players/pages, changing counts/totals, or contributions inconsistent with the reported sum cause that chart to be skipped. HTTP 429 stops the remaining verification checks without advancing exploration past unchecked candidates. Verification failures are reported; if they leave no usable recommendations, the previous clan playlist is preserved. A successful empty result can replace old output. Clan-specific failures do not stop other clans from refreshing. A successful empty membership removes obsolete generated clan files.

## Refresh performance

Screening caches live under `RankedPractice/clan-search`. They store compact public chart metadata and clan totals, with a schema version, BeatLeader rating epoch and expiry timestamp. Malformed, expired, future-dated or incompatible caches are ignored and fetched again. Scores, attempts, clan membership, the global ownership snapshot, and each verified chart's rankings/contributions remain fresh. Cache-write failures warn without preventing playlist generation.

Two verification jobs can run together. All clan requests pass through a shared gate: starts are at least 90ms apart across independent maps and 225ms apart for repeated calls to the same endpoint path (query pages share that endpoint). This follows BeatLeader's [per-endpoint rate configuration](https://github.com/BeatLeader/beatleader-server/blob/master/Startup.cs) and its rate limiter's [path-based counter](https://github.com/stefanprodan/AspNetCoreRateLimit/blob/master/src/AspNetCoreRateLimit/CounterKeyBuilders/PathCounterKeyBuilder.cs). HTTP 429 stops new requests; already-running requests are joined. A clan with no usable results preserves its prior playlist, including subsequent clans skipped after a rate limit. If another worker cannot start, processing stays sequential. Existing HTTP diagnostics aggregate the extra worker's requests, connections and transfer time.

The default 64-chart verification depth and final capture-first ordering are unchanged. Rechecking previous picks uses that existing budget. Per-chart PB lookups use a prebuilt table rather than rescanning the whole profile. Both public-profile replays retained the same selected charts and ordering before and after transport optimization; those audits compared the same exploration position, rather than consecutive saved refreshes.

| Captured main-clan audit | Clan-stage requests | Response data |
| --- | ---: | ---: |
| Before optimization | 187 | About 28 MiB |
| New, no screening caches | 142 | About 28 MiB |
| New, both screening caches fresh | 130 | About 5 MiB |

Counts exclude the shared player-history load and cover image. If only the catalog cache is fresh, clan totals are fetched again. These are request/data measurements from captured responses; they are not Quest wall-clock timings. First refreshes still download the full public metadata, and every refresh still performs live map verification.

A controlled replay using the production pacing/two-worker path and simulated 200ms latency for each request completed the clan stage in **17.4 seconds uncached** and **13.7 seconds with both caches fresh**. This includes fixture I/O/repaging and model processing, excludes the already-shared profile-history load, and performs no real network requests. These timings demonstrate the scheduling improvement; actual headset/network refresh time still requires measurement.

## Secondary clans

BeatLeader assigns conquest contributions only to a player's primary clan. The mod resolves primary order from `clanOrder`, using clan ID as the fallback, matching the server. All-clans mode labels every secondary playlist and preview as **what-if primary clan**.

For each hypothetical chart, the existing eligible PB is transferred into the target clan and removed from the current primary clan before establishing the gameplay baseline and strongest rival. Only the additional improvement counts as projected gameplay gain. A chart already captured by that hypothetical transfer is omitted. The mod never changes the player's primary clan or account settings.

Server rules and data sources: [clan weighting and primary attribution](https://github.com/BeatLeader/beatleader-server/blob/master/Utils/ClanUtils.cs), [associated scores and clan rankings](https://github.com/BeatLeader/beatleader-server/blob/master/Controllers/LeaderboardController.cs), and the [public API](https://api.beatleader.com/swagger/index.html).

## Validation snapshot: October 31, 2026

For release 0.1.24, the optimized regression runner passed 3,716 recommendation/API checks, 55 playlist-writer checks, and 16 HTTPS checks (3,787 total). These include automatic selection across 1-star and 22-star charts, screening-cache correctness, live verification, bounded workers, per-endpoint pacing and worker HTTP statistics, plus legacy cap/PB filtering, later pages, PPv3/PPv4 predictions, mode isolation, independent failures, and replacing mode titles/descriptions in the same playlist files. Direct-cap tests cover every slider threshold, 0% disabling the cap, PP independence, real failure evidence and multiple qualifying star regions.

The Quest arm64 build passed. `RankedTools-0.1.24.qmod` was schema-validated for Beat Saber `1.40.8_7379`, and its arm64 library matched the final build with all 24 entries, the CA bundle and packaged assets present. Symbol inspection confirmed Mbed TLS and no OpenSSL implementation. Exact dependency source archives and checksums accompany the release. Automated validation does not replace testing the final cap change and UI on the headset.

The consecutive-refresh regression uses fixed responses and advancing saved exploration. The original implementation retained 0 of 16 tracks between refreshes; the corrected search retains all 16 when discoveries have equal or lower clan value. A second fixed-input scenario confirms that genuinely greater weighted contributions replace prior recommendations as they are discovered. Previously checked negligible contributions no longer dominate the capture/progress quotas. Both scenarios stay within 64 verified charts per refresh. Changed rival scores, completed targets, failed-pick retries, restart persistence, primary-clan changes, and independent secondary-clan transfers are also tested.

Two public profiles were evaluated through the production clan implementation using separately captured API responses. These audits used complete PB histories, a 50% minimum clear estimate, automatic selection across all star ratings, and 16 tracks; they did not import attempts or change playlists/cursors.

| Public player ID | PB rows | Eligible charts | Verified charts / failures | Selected estimates |
| --- | ---: | ---: | --- | --- |
| 76561199146457878 | 2,885 | 1,642 | 64 / 0 | 16 captures |
| 76561198325198406 | 11,109 | 884 | 64 / 0 | 1 capture, 15 progress |

The first audit included a 3.81-star capture and an existing-score improvement. The second included a capture with only about 3.15 additional weighted clan PP against a 0.70-PP gap. These are snapshot/model estimates rather than observed captures. A temporary HTTP 429 interrupted second-profile collection; collection stopped, then completed with sequential pacing. Regression checks verify that production verification also stops on HTTP 429 and preserves output when no usable recommendations remain.

The post-play refresh regression confirms that recording a PB at the predicted score removes that chart from the next recommendation result when the skill estimate has not increased. Headset UI layout, actual refresh duration, and gameplay/post-play behavior require device validation; clearability and capture estimates remain heuristic.
