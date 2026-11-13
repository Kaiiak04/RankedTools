# RankedTools PCVR 0.1.28 preview 1

Windows x64 testing builds target Beat Saber 1.40.8 and 1.44.1; use the matching ZIP. Adds AccSaber account selection, AP recommendations by category, category ability charts, local attempt recording and three-service Sync. Separate New and Repeat Tracks defaults off; Required Sync Scoreboards defaults to 2 and currently allows 2–3. AccSaber personal playlists start off and default to Overall. Supplied personal covers and the replacement Sync image are bundled unchanged. AccSaber clans remain deferred.

The shared engine uses a 30-day sigmoid accuracy fit for all services and categories. It also enables Mbed TLS threading and records detailed curl/TLS errors in persistent last-failure reports. These PC builds still need in-game testing; the maintainer's November 13 Quest result applies to the QMOD only.

Copy every folder from the ZIP into the game folder while the game is closed. Required mods are listed in README.md. Follow TESTING-THIS-BUILD.md and report the exact package, setting combination, error and chart identity for problems.

Automated native, managed/binding and HTTPS checks run against the packaged binaries. A live AccSaber-only refresh using a public profile fetched the complete 507-map/333-score sample and produced 16 recommendations with all three category fits. This validates one public snapshot, not in-game behavior or every account.
