# RankedTools PCVR 0.1.26 preview 4 — menu layout fix

Fixes the oversized spacing and clipped controls reported in Mod Settings and the RankedTools menu. Rows now use explicit compact heights. Scroll content stays within the viewport width, and long text wraps without widening the panel.

The account picker keeps **Name / URL / ID**, search buttons, page buttons, and Clear/Cancel visible while only its results scroll. Song details and ability charts use the space below the navigation buttons. The header shows a short refresh status; the full status and warnings remain readable below the song/chart information.

The previous menu-opening, integer clear-rate, and immediate account-query fixes are included. Saved settings and recommendation behavior are unchanged. Controller scrolling/input behavior is unchanged; the reported continuous scrolling was stick drift.

Automated checks cover both target builds, pinned BSML factories, bindings and control types, compact row bounds, and remaining scroll space at several panel heights. Preview 3 fails the new layout checks. Native regressions and real HTTPS through the packaged engine also run. These checks do not render the interface in VR.

Close Beat Saber and copy all contents of the ZIP matching your game version into the folder containing **Beat Saber.exe**. Windows combines the folders automatically. Use the dependency list in the included README.

**The new layout needs a VR retest.** On 1.40.8, a tester confirmed the previous preview's menu buttons and recommendation refresh work, then reported the sizing problems corrected here. Check that account input stays reachable, song details can be read to the final line, warnings do not push controls off-screen, and both Back buttons work. See `TESTING.md` for the full checklist.

Separate ZIPs target **1.40.8** and **1.44.1**. The latter continues to compile against the agreed public **1.42 API family** with Legato; actual 1.44.1 compatibility remains pending.
