# RankedTools PCVR 0.1.26 preview 2 — menu fix

Fixes invalid menu markup that can leave a blank RankedTools screen and an unfinished opening transition, as reported on **Beat Saber 1.40.8**. The fix is included in both **1.40.8** and **1.44.1** ZIPs.

- Removed unsupported `<bsml>` wrappers from the recommendations and settings menus.
- Replaced the unsupported `scrollable-container` tag with the `scroll-view` supported by both pinned PC BSML versions.
- Build the recommendations view before starting the opening transition, so a parser error is caught while the existing menu is still usable. Start automatic refresh after the presentation finishes.
- Added a visible **Back** button and guarded repeated close requests. The header back arrow also returns to the main menu.
- Expanded automated checks to validate every menu tag against each target's actual pinned PC BSML dependency. The old preview fails this new check.
- Clarified the Windows copy/paste installation instructions.

Close Beat Saber, open the ZIP matching your game version, and copy everything inside into the folder containing **Beat Saber.exe**. Windows combines the folders automatically. Install the dependencies listed in the included README first.

**The menu fix still needs in-game retesting.** Automated checks verify compiled bindings and dependency compatibility; they cannot verify VR interaction. Please test opening, closing and reopening the RankedTools screen, including closing during a refresh, and include `Logs/_latest.log` if it fails.

The 1.44.1 preview continues to compile against the agreed public **1.42 API family** with Legato. Its in-game compatibility remains pending. See the included README and `TESTING.md` for dependency details and the full checklist.
