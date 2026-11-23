# RankedTools 0.1.25 — Sync

- Added **Sync**, for charts ranked on both ScoreSaber and BeatLeader with a posted score on exactly one.
- Matches the exact song hash, characteristic and difficulty, across all characteristics. Multiple matching charts share a song entry; previews show which leaderboard is missing each score.
- Includes **all** matches, ignoring playlist song count, Simple Mode, clear-rate, star and PP filters. No Fail scores count as posted scores; local attempts do not.
- Select both accounts and turn on **Create Sync playlist** in Mod Settings. This works independently of the personal playlist toggles and is off by default.
- Complete catalogs and score histories are checked before writing. Incomplete data or a failed save preserves the previous Sync file. An empty successful refresh clears its entries.
- Bundled the supplied hand-drawn cover unchanged for Quest and both Windows x64 PCVR targets.

Developed on `codex/pcvr`. Both PCVR targets compile and pass automated native/managed checks and HTTPS smoke tests. The 1.44.1 preview still uses 1.42 API-family references. The Quest build and regression suites pass. In-game validation remains pending on each platform/version.
