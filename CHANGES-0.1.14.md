# RankedTools 0.1.14

Clear estimates now use actual successful and failed attempts. Successful attempts below a personal best count too. Partial-run accuracy never trains the full-song accuracy curve or enters the PP profile. A failed song remains eligible for Not Played.

## Local recording

**Record local attempts** is enabled by default. Game hooks capture solo custom-map results independently of leaderboard uploads. Stored fields include chart hash/characteristic/difficulty, timestamp, outcome, elapsed song time, modifier flags, and practice state. Invalidated game results are skipped. Multiplayer and Party mode are not recorded locally.

Player IDs are captured from RankedTools settings when gameplay starts. They are not automatically verified against a signed-in leaderboard account. Use your own IDs, or disable local recording when viewing another player's recommendations. Disable it during replay playback as well: third-party playback that does not invalidate game results cannot reliably be identified through this game callback.

Saving happens on a background thread. Each service/player has a separate file under `/sdcard/ModData/com.beatgames.beatsaber/Mods/RankedPractice/attempts/`. No replay files, credentials, or uploads are created by this feature. A game termination before the background write finishes can lose that last event.

## History imports and persistence

**Import attempt history** is enabled by default. BeatLeader uses `/player/{id}/scoresstats` for all outcomes. Private statistics remain inaccessible without authentication; the mod does not read tokens. ScoreSaber uses `/api/v2/players/{id}/scores?personalBest=all` for the default rating realm (ID 1). The documented realm parameter is retried without it after HTTP 400, and returned realm IDs are validated before accepting the import.

Initial backfill covers up to two years and 20,000 raw records. Subsequent imports overlap the latest stored provider timestamp by two minutes. API history depends on the client/version that uploaded it, server visibility and retention. Interrupted imports keep the previous complete cache and do not advance the cursor past missing pages. Reaching the explicit backfill cap saves the newest records and reports that older history was omitted.

Each file retains the newest 20,000 events. A lock protects read/merge/write transactions, and a temporary file is renamed over the original after a successful write. Local results saved during an API refresh are merged rather than overwritten. Damaged or unsupported history files are preserved and reported. Disabling import stops remote fetching but still uses existing cached/local evidence; disabling recording stops new captures. Neither toggle deletes history.

Provider IDs deduplicate repeat imports. Cross-source matching requires the same chart, outcome, practice/modifier state, song time within one second, and timestamps within 30 seconds. Matching is one-to-one; provider records retain the matched local event ID. Distinct provider IDs are not collapsed. Very delayed uploads, clock differences, or mismatched song times can prevent a local/API match.

Attempts with unknown ratings are matched to profile/chart metadata or looked up separately for each service. BeatLeader's hash lookup returns all charts; ScoreSaber uses the full hash/characteristic/difficulty lookup because its hash-only route returns difficulty identifiers without ratings. At most 100 lookup queries run per refresh. Successful unrated lookups are retried after seven days; lookup failures are reported. Unrated attempts do not train clearability.

## Clearability model

- Actual-attempt training includes only unmodified, non-practice Standard clears and failures with ranked stars. Modified runs, quits, restarts, and practice are stored for audit but excluded from this training.
- Weights decay as `exp(-ageDays / 365)`. The total actual-attempt weight of each chart is capped at four while preserving the recency-weighted clear/failure balance.
- Nearby stars use a Gaussian kernel with width 0.8 stars and the existing two-observation prior. Candidates with chart-specific evidence receive a second smoothed estimate combining their own outcomes with the general prediction.
- Earlier failures remain after a later clear. A leaderboard PB is not added as another success if real clears for that chart are already recorded. When only failures are recorded, the known leaderboard clear remains fallback evidence.
- No Fail scores remain weaker fallback evidence on charts without a normal clear or usable actual attempts.
- The preview now lists nearby clears, failures, and NF runs. Thresholds and Simple Mode continue to use the same clear estimate; 0% still ranks by theoretical weighted PP gain.

The percentage remains a heuristic, not a calibrated guarantee. Charts of similar stars can have different patterns, stamina requirements, or difficulty spikes.

## Validation

All 518 recommendation/history regression checks and 27 playlist-writer checks passed. They cover both providers' real response schemas, pagination, balanced clear/failure evidence, modifier/practice exclusions, recency, chart-specific influence, repeated import IDs, local/API matching, persistent aliases, concurrent writes, corruption preservation, API outage fallback, local-only rating lookups, and ScoreSaber realm validation. The Quest arm64 build passed and validates the game bindings and hooks. Headset execution still needs an in-game check.
