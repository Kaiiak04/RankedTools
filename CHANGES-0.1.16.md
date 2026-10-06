# RankedTools 0.1.16

Fixes overlapping and overflowing recommendation-preview text, and explains BeatLeader's attempt-history permission warning.

## Preview layout

The BSML settings-container helper disables child-height control. The previous preview set preferred text heights, but the parent did not apply them; text rectangles started at zero height. Its wrapper also fitted its width to the contents, so a long status message could expand beyond the viewport.

The preview now enables child-width and child-height control, stretches content to the viewport width, and uses TextMeshPro's preferred height after wrapping. It reuses each text object's existing LayoutElement instead of adding a duplicate. Horizontal content fitting is disabled on the scroll wrapper; vertical fitting is preserved. Playlist and song navigation each use a horizontal row. Text is left-aligned with normal font style. After a preview update, layout is rebuilt and the scroll extent is refreshed so long statuses or song titles remain scrollable.

## BeatLeader attempt access

An HTTP 401 from the attempt-history endpoint now explains that public statistics must be enabled on the BeatLeader profile for anonymous import. The mod does not change account privacy settings or read authentication tokens. Cached/local attempts and leaderboard best scores remain available, and an attempt-import denial does not fail an otherwise successful playlist refresh.

The success status is shortened to "Playlists updated." Each attempt-history warning starts on a separate line; PlaylistManager's reload instruction appears once below the recommendation details. The recommendation models and ranking rules are unchanged.

## Validation

All 1,160 recommendation/history checks and 27 playlist-writer checks passed. The permission-denial checks confirm that best-score loading succeeds, cached attempts are preserved, and BeatLeader's warning explains public statistics and local fallback. The Quest arm64 build passed. Layout configuration was checked against the installed BSML 0.4.55 implementation. Headset rendering and navigation still need an in-game check.
