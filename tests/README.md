# Recommendation regression checks

`recommendation_tests.cpp` compiles the production recommendation and API code directly. HTTP responses are injected, so tests do not access the network or depend on an installed game. Public API samples are kept in `fixtures` for schema and PP formula checks.

On Windows, use an installed Android NDK and the existing Docker Desktop WSL distro:

```powershell
pwsh ./tests/run.ps1
```

The runner discovers the NDK installed by QPM. Override its compiler with `-Compiler <absolute clang++.exe path>`. The static test executable targets x86_64 Android and runs in WSL; the actual mod build continues to target Quest arm64. For another WSL distro, pass `-Distro <name> -LinuxWorkspace <workspace mount path>`.

Checks cover timestamp parsing and recency, all supported response shapes, unique chart parsing, effective modifier stars, mixed-characteristic profile weighting, NF exclusions, both public PP formulas, histories beyond the old page caps, skipped/unrated rows, failed/incomplete pagination, cache reuse, later recommendation candidates, playlist song limits, empty/malformed clan membership, worker settings ownership, all 20 clear-rate steps, cautious target stars, merged gain sorting, and choosing the best chart before deduplication.

Accuracy checks cover monotonic weighted regression, cascading pools, recency and missing-date weights, equal-star observations, interpolation, empty and sparse profiles, endpoint holds and extrapolation limits, training exclusions, modified stars, input-order independence, and both recommendation modes on both leaderboards. A noisy-profile sweep verifies finite, bounded predictions that never increase as stars increase.

Account checks cover profile URL hosts and escaping, aliases and linked BeatLeader IDs, exact 64-bit ID parsing without floating-point rounding, ScoreSaber's minimum search length and nested rank fields, search pagination, duplicate display names, malformed/unavailable responses, superseded requests, cancelled searches, independent service state, and bounded PNG/JPEG avatar dimensions. They exercise production lookup/parsing/state helpers with injected HTTP responses. The actual settings UI and BeatLeader's signed-in export need on-device validation.

`playlist_writer_tests.cpp` additionally executes the production playlist writer against a workspace-local sandbox. It verifies combined playlist serialization and switching both directions between split and combined layouts, including preservation of other services, clan playlists, backups, and user playlists. The sandbox is removed after a successful run. A production build uses the ordinary Quest PlaylistManager directory; only the test compilation overrides its path.

Fixture sources, captured during review and fix verification in October 2026:

Attempt tests also cover real BeatLeader `/scoresstats` and ScoreSaber v2 `personalBest=all` responses, BeatLeader's sibling song/chart hash-lookup shape, clear/failure balance, practice/modifier exclusions, recency, chart-specific predictions, bounded chart influence, local/API deduplication, concurrency, damaged-file preservation, offline fallback, and ScoreSaber's documented-realm HTTP 400 workaround. Persistent test files use only `tests/attempt-sandbox`; successful runs remove their explicitly named fixtures.

- BeatLeader public `/player/{id}/scores` and `/maps` responses, reduced to representative rows including FS and OneSaber. NF scores use `modifiers=NF`.
- ScoreSaber public `/api/player/{id}/scores`, `/api/leaderboards`, and `/api/v2/leaderboards` responses. v2 uses `status=RANKED`, `realmId=1`, and star bounds.
- ScoreSaber `/api/v2/realms/1/pp-curve` for its published accuracy multiplier points.

Pagination metadata in reduced fixtures is adjusted to the retained row count. Synthetic cases supply deterministic mock pages, including network failure and invalid server responses. `.review/review_tests.cpp` is the historical pre-fix reproducer and intentionally asserts the old defects; use this suite for post-fix validation.
