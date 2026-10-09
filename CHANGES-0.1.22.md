# RankedTools 0.1.22

Speeds up recommendation refreshes by retaining one HTTPS client per worker thread. Requests reuse live connections, DNS results and TLS sessions instead of opening a new connection for every page. Request options and callback pointers are reset between transfers; byte limits, timeouts, HTTPS-only redirects and certificate verification remain in place. Handles are never shared between threads and are released when their worker exits.

ScoreSaber now refreshes concurrently with BeatLeader. BeatLeader recommendations and clan playlists stay on one worker so they can share history and the automatic star range. Completion waits for both workers, and an exception in one service is reported without interrupting the other. Devices unable to start the additional thread fall back to sequential work. Scores and the entire candidate star band remain fresh on every refresh, with existing page pacing and recommendation ranking unchanged.

ScoreSaber's current v2 routes can reject the numeric `realmId=1` query parameter as a string. The existing attempt-import workaround now remembers the fallback across pages and also covers candidate-map and attempt-rating lookups. Returned map ratings are checked against the default realm before use. Other failures, including rate limits, do not trigger this retry.

The success message shows total refresh time. Logs include elapsed time, HTTPS request counts, new connections and request time for BeatLeader, ScoreSaber and clan work, plus the overall duration.

A small desktop network sample fetched the same three candidate pages per provider: fresh connections took 1.97s for BeatLeader and 1.19s for ScoreSaber; reused connections took 1.31s and 0.45s respectively. Each provider used one new connection rather than three. This is an illustrative request-only sample using desktop curl, not a full Quest refresh benchmark; API load and network conditions vary. Concurrent work provides an additional opportunity to shorten the total when both providers are enabled. Large histories and first-time attempt backfills can still be slow.

The normal Scotland2 loading phase introduced in 0.1.21 is retained. Startup, UI behavior and real refresh durations still need headset testing.

Validation: 2,118 recommendation/API/concurrency checks, 40 playlist-writer checks and 16 HTTPS-client checks passed. The Quest arm64 build and QMOD manifest schema validation passed. The package's version, game target, normal loading phase, all 10 archive entries, current-build binary and bundled file hashes, and matching generic/versioned QMOD copies were verified.
