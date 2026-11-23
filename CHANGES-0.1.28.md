# RankedTools 0.1.28

- Added AccSaber accounts, AP recommendations, category selection and separate Complexity ability charts. AccSaber playlists start off; enable them after selecting an account. AccSaber clan playlists are deferred.
- Expanded Sync to BeatLeader, ScoreSaber and AccSaber. **Required Sync Scoreboards** defaults to 2 and currently allows 2–3. Selected accounts participate even when their personal playlists are off. Sync lists every missing service and preserves previous output when a participating service fails.
- Switched personal accuracy predictions and ability charts to a **30-day sigmoid** on every leaderboard. Thirty days is an exponential age-decay time constant, not a history cutoff. AccSaber categories retain independent fits; the separate clearability model is unchanged.
- Combined PP/AP Gain playlists are now the default. Enable **Separate New and Repeat Tracks** for Not Played and To Improve playlists; it replaces the old Simple Mode control.
- Included BeatLeader PPv4.1 calculations and rating-cache migration from 0.1.27. Refresh after installing to update recommendations and cached ratings.
- Fixed the HTTPS library's threading configuration for concurrent requests. Transfer errors now include numeric curl codes and available TLS details. The final refresh status retains the actual failure, and `last-refresh-error.log` survives successful retries and game restarts.
- Added the supplied AccSaber playlist artwork and replacement Sync cover on both platforms.

Quest targets **Beat Saber 1.40.8_7379**. The release QMOD is byte-for-byte the HTTPS-fix testing QMOD reported working by the maintainer on November 13, 2026. That report covers their usual workflow, rather than exhaustive acceptance of every feature/account combination.

Windows x64 packages for **1.40.8** and **1.44.1** remain PCVR previews and need in-game testing. Automated native, managed/binding, packaging and HTTPS results are recorded separately from headset results. See [the testing checklist](TESTING-0.1.28.md).
