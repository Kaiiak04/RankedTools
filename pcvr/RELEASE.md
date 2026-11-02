# RankedTools PCVR preview 0.1.24

RankedTools now has a PCVR preview for **Beat Saber 1.40.8 and 1.44.1**!

It includes both leaderboards, PP Gain / Not Played / To Improve playlists, personalized and legacy clan playlists, account search, song details, ability charts, and local attempt history. The recommendation engine is shared with the Quest version.

Download the ZIP matching your game version and extract **all** its contents into that game's folder. Install the core dependencies listed in the included README first. BeatLeader and ScoreSaber's in-game mods are optional.

**Both versions still need in-game testing.** The 1.44.1 build uses the compatible 1.42 API family because exact public 1.44.1 references are unavailable. Automated checks and HTTPS smoke tests pass; those do not verify VR menus or gameplay integration.

PlaylistManager 1.7.3 is available for 1.40.8. For 1.44.1, the README links a community 1.42-family playlist browser build, whose actual 1.44.1 behavior also needs testing. RankedTools can generate playlist files without it.

Please use the included `TESTING.md` checklist and report your game/dependency versions with any issues. Quest development remains on the existing build path; this port is being prepared on `codex/pcvr` before bringing both platforms together on `main`.
