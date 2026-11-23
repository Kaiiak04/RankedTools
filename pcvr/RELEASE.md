# RankedTools PCVR 0.1.31 preview 1

Windows x64 testing builds target Beat Saber 1.40.8 and 1.44.1. Use the ZIP matching your game version.

Ranked BeatLeader OneSaber charts can appear in personal and both clan playlist modes. Standard and OneSaber share the same accuracy and clearability models. Song details identifies OneSaber picks.

**BeatLeader Modifiers** defaults to off. Enable it and refresh to consider ranked speed and scoring modifiers in personal recommendations and both clan playlist modes. Faster plays use clearability estimates at their effective stars. Personalized clan capture targets use the recommended modifiers, and legacy clan playlists apply their star cap to each option. Local and imported BeatLeader attempts now deduplicate matching runs when their timing fields differ.

Copy every folder from the ZIP into the game folder while the game is closed. See [installation requirements](README.md) and the [tester checklist](TESTING.md).

Automated native, managed, and HTTPS validation results for the bundled binaries are in `validation.json`. The maintainer confirmed the modifier features on Quest. PCVR in-game checks are pending.
