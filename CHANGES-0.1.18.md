# RankedTools 0.1.18

Maps to Conquer playlists now use their clan's own BeatLeader icon. Main-clan mode uses the icon of the first joined clan in the configured BeatLeader clan order. All-clans mode embeds the respective clan's icon into each separate playlist.

The existing clan maps API response provides the clan's tag and icon URL under `container`, avoiding a separate metadata request. Clan membership from the player profile remains the authority for which clans to include and their order. Icon bytes are downloaded over HTTPS on the refresh worker, recognized as PNG/JPEG, and embedded as a base64 data URI in the `.bplist`. Icons are fetched again on each refresh so later clan artwork changes are picked up. Each image request has an eight-second timeout and four-MiB response limit.

Missing icons, download errors, unsupported/truncated payloads, and oversized images fall back to the existing bundled Maps to Conquer cover. The refresh reports an icon warning while still generating the clan's songs. Failure of one clan's image does not affect another clan's cover. Clan map/profile fetch errors retain the existing behavior of preserving playlists.

Validation: 1,997 recommendation/history/chart/clan checks and 40 playlist-writer checks passed. New checks cover reversed membership order, main versus all-clans selection, independent covers, missing or mismatched metadata, failed/invalid/oversized downloads, bounded binary transfers, exact image serialization, and the bundled fallback. The live CATT map response and public PNG icon were checked; a saved PNG fixture keeps regression checks independent of the network. Quest compilation and QMOD archive verification passed. In-game PlaylistManager rendering remains untested.
