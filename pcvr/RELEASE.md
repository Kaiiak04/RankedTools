# RankedTools PCVR preview 0.1.25

This update adds **Sync** to the PCVR previews for **Beat Saber 1.40.8 and 1.44.1**.

Select both accounts and enable **Create Sync playlist** in Mod Settings. Sync finds exact charts ranked on both ScoreSaber and BeatLeader with a posted score on only one, helping you fill the other leaderboard's scores. It includes all matching charts across all characteristics, ignores the playlist song-count setting, and uses the new supplied cover unchanged. No Fail scores count as posted scores; local attempts do not. Song previews show which service is missing each chart. Failed or incomplete refreshes preserve the previous playlist.

It includes both leaderboards, PP Gain / Not Played / To Improve playlists, personalized and legacy clan playlists, account search, song details, ability charts, and local attempt history. The recommendation engine is shared with the Quest version.

Download the ZIP matching your game version and extract **all** its contents into that game's folder. Install the core dependencies listed in the included README first. BeatLeader and ScoreSaber's in-game mods are optional.

**Both versions still need in-game testing.** The 1.44.1 build uses the compatible 1.42 API family because exact public 1.44.1 references are unavailable. Automated checks and HTTPS smoke tests pass; those do not verify VR menus or gameplay integration.

PlaylistManager 1.7.3 is available for 1.40.8. For 1.44.1, the README links a community 1.42-family playlist browser build, whose actual 1.44.1 behavior also needs testing. RankedTools can generate playlist files without it.

Please use the included `TESTING.md` checklist and report your game/dependency versions with any issues. Quest development remains on the existing build path; this port is being prepared on `codex/pcvr` before bringing both platforms together on `main`.
