# RankedTools 0.1.27 — BeatLeader PPv4.1

- Updated the shared Quest/PCVR calculation to BeatLeader's November 5 PPv4.1 formula: revised accuracy curves, linear accuracy/tech ratings, accuracy-dependent pass PP, mid-star accuracy bonus, star scaling, and stronger sparse-note penalty.
- Personal predicted PP, weighted gains, clan progress and capture targets now use the new calculation. Existing best-score PP continues to come from the API.
- Invalidated cached BeatLeader attempt ratings, clan screening responses, and learned clan impact evidence from the previous reweight. Refresh playlists after installing to rebuild them with current data.
- Added 4,518 independently generated upstream PP references and three current API scores, plus regression checks for migration from PPv4 caches. See [compatibility details](docs/PPV4.md).
- Quest remains on Beat Saber 1.40.8_7379. Windows x64 previews target 1.40.8 and 1.44.1; in-game validation remains pending.
