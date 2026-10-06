# RankedTools 0.1.13

- Replaced Comfortable/Stretch/Max PP presets with a Min Clear Rate slider: 0–95%, increments of 5%, default 50%. 0% keeps raw weighted PP ranking. Higher thresholds filter unplayed charts; 95% shifts the search band lower when needed.
- Added Simple Mode, off by default. One PP Gain playlist per enabled leaderboard combines eligible unplayed charts and improvements, sorted by theoretical weighted PP gain. It chooses the highest-gain chart per song and applies one shared track limit after merging. The unplayed clear-rate filter still applies, and clan playlists remain separate.
- The recommendation preview follows the selected playlist layout and displays the appropriate baseline for improvements.
- Successful refreshes remove the old layout's exact generated filenames. Disabled services also remove incompatible layout files. Other services, clan playlists, user playlists, and songs are preserved. Failed fetches do not replace or prune the enabled service's previous layout.
- Replaced all five old covers and added two PP Gain covers with neon artwork based on the official leaderboard logo references. Final assets and prompts are in [assets/ARTWORK.md](assets/ARTWORK.md), with packaging copies at the workspace root. The QMod includes all seven covers.

Validation: 404 recommendation checks and 27 real playlist writer/cleanup checks; Quest arm64 compilation and QMod manifest/archive validation. Package targets Beat Saber `1.40.8_7379`. Headset runtime/UI testing remains unverified.
