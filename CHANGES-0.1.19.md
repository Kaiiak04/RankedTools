# RankedTools 0.1.19

Replaces all seven bundled playlist covers with the project owner's supplied hand-drawn artwork. The original 512 × 512 PNG files are copied unchanged into both the project root (used for QMOD packaging) and `assets/`. Artwork notes and the README now describe the current covers.

Maps to Conquer continues to use the actual clan icon whenever available, independently for each clan playlist. The new bundled clan cover is only the fallback. Recommendation behavior is unchanged.

Validation: all source/root/assets image hashes match; all seven images in the QMOD match the supplied files. Existing 1,997 regression checks and 40 playlist-writer checks passed with the new artwork. Quest build, manifest schema, version, and archive verification passed. In-game appearance remains untested.
