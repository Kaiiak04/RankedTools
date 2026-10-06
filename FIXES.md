# RankedTools 0.1.12 review fixes

All eight findings from the original review are addressed:

1. Not Played fetches the entire selected star band before clearability filtering, ranking, and distinct-song limiting. API parsing emits one entry per chart instead of recursively treating nested metadata as extra entries.
2. The weighted profile includes PP-bearing ranked scores across all characteristics. Candidates remain Standard charts, but their gain accounts for existing OneSaber and other ranked scores.
3. ScoreSaber `timeSet` ISO timestamps are converted to Unix seconds in UTC, including fractional seconds and explicit offsets. Both accuracy and clearability observations now receive their intended age weights.
4. BeatLeader score observations use returned `modifiedStars`, or derive it from `modifiersRating` and `modifierValues`. Faster/Slower/Super Fast scores train at the effective difficulty. NF's zero-PP penalty does not erase attempted difficulty evidence.
5. ScoreSaber candidate queries use its v2 ranked leaderboard endpoint, supported 100-chart pages, and server-side star bounds, and paginate to the reported end.
6. Both score histories paginate without fixed page caps. Termination uses raw rows and API metadata rather than the number of usable training entries. Unrated completed scores are retained for played detection. Histories are reused within one refresh and are cached only after a complete successful fetch.
7. Verified empty clan membership is a successful empty result, allowing the existing cleanup path to remove stale generated clan playlists. Missing/malformed membership and HTTP failures remain errors and preserve files. Stale clan order tags are ignored.
8. The UI thread captures every setting before starting the worker. The worker owns an immutable copy and never reads mutable config. Settings changes apply to the next refresh.

An additional iterator invalidation in clan difficulty selection was corrected by copying the selected difficulty before replacing the vector.

Validation: production-code regression suite (300 checks), successful Quest arm64 build, and QMod manifest/archive validation. Tests include public API response samples and deterministic multi-page/error cases. The package remains targeted at Beat Saber `1.40.8_7379`. No headset testing was performed during this change; recommendations remain estimates based on the available history and selected star band.
