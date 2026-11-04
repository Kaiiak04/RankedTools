# RankedTools PCVR 0.1.26 preview 3 — settings fix

Corrects an integer/float mismatch in the **Min Clear Rate** control that prevents Mod Settings from initializing. The slider is integer-only, so PC BSML reads a boxed `Int32`; the previous host returned a boxed `Single`. The host property and percent formatter now both use integers. Existing saved settings use the same format.

Account-search input now applies when the keyboard entry is accepted, before **Find** is clicked, and updates when the picker clears or resets the query. The previous Back-button and menu-markup fixes are included.

Automated checks now validate the backing type, getter/setter availability and formatter signature of every settings control, along with tag names, UI bindings, and immediate account-query wiring. Both preview-2 plugins fail the new clear-rate type check; both corrected plugins pass.

Close Beat Saber and copy all contents of the ZIP matching your game version into the folder containing **Beat Saber.exe**. Windows combines the folders automatically. Use the dependency list in the included README.

**The corrected settings page still needs a VR retest.** Check that Mod Settings loads, Min Clear Rate displays and saves 0%, 50% and 95%, and account search uses the entered query before closing settings. If an error page remains, include the exception from the latest log in the game's `Logs` folder.

Separate ZIPs target **1.40.8** and **1.44.1**. The latter continues to compile against the agreed public **1.42 API family** with Legato; actual 1.44.1 compatibility remains pending. See `TESTING.md` for the full checklist.
