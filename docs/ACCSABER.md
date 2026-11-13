# AccSaber support

AccSaber personal playlists are available in the shared Quest/PCVR source.
**Create AccSaber playlists** defaults to off. Select an AccSaber account by
name, numeric ID, or `https://accsaber.com/players/<id>`, then enable it.
Account selection stores the canonical ID returned by AccSaber; it does not
assume your selected BeatLeader or ScoreSaber account is the same person.

## Personal recommendations

**AccSaber Category** defaults to **Overall**. True Acc, Standard Acc and Tech
Acc restrict the same playlist set to one category. Overall compares weighted
AP gains from categories that count toward Overall. Each category has its own
accuracy fit, AP curve and profile-position weighting; scores in a different
category do not displace its plays. Complexity is an independent 0–14 rating,
including a valid zero, and is never interpreted as BeatLeader/ScoreSaber stars.

With **Separate New and Repeat Tracks** off, the mod writes **AccSaber - AP
Gain**. Turning it on writes **Not Played** and **To Improve** instead. The
existing track limit, best-chart-per-song rule and clear-rate setting apply.
Combined playlists rank by weighted AP gain; split Not Played playlists apply
the existing chance adjustment. Filenames stay stable when the category
changes, including when an empty result replaces the previous playlist.

Predictions use the shared 30-day sigmoid to fit eligible, ranked Standard PBs
separately per category against complexity. Thirty days is an exponential
age-decay time constant; older scores still contribute. True Acc, Standard Acc
and Tech Acc all use the same model parameters with independent observations.
NF and speed-modified scores do not train normal-speed accuracy. A category
without eligible observations has no generic 85% fallback. Overall can still
recommend from other categories; a wholly unsupported selection preserves the
previous playlists and explains the missing evidence. Ability Chart has a
separate Complexity view for each selected category.

Published AP, including modifier AP, anchors profile-change calculations.
NF-only charts retain the existing Not Played eligibility policy. A projected
replacement must strictly beat the existing **unmodified score**, even for NF;
a higher hypothetical AP alone cannot replace an AccSaber PB. AP interpolation,
position weighting, accuracy rounding and six-decimal AP rounding follow the
[upstream calculation](https://github.com/accsaber/accsaber-reloaded-backend/blob/7a97d2424a6f89d48245335645e86c1ee65966bf/src/main/java/com/accsaber/backend/service/score/APCalculationService.java)
and [PB rules](https://github.com/accsaber/accsaber-reloaded-backend/blob/7a97d2424a6f89d48245335645e86c1ee65966bf/src/main/java/com/accsaber/backend/service/score/ScoreService.java).

Local attempts use the AccSaber ID captured at gameplay start. Optional imported
BeatLeader/ScoreSaber evidence uses only identities explicitly linked by the
AccSaber profile and resolves chart ratings against the fresh AccSaber catalog.
There is no per-chart AccSaber history crawl. Import failures warn and retain
cached/local evidence; they do not prevent personal recommendations.

## Sync across selected accounts

**Required Sync Scoreboards** is an integer, default **2**, currently **2–3**.
The maximum follows the supported-provider count. A chart must be ranked on
at least that many **selected services**, have a posted score on at least one,
and be missing a posted score on another. Every missing service is shown.
AccSaber's categories collectively count as one service.

Selected accounts participate even when their personal playlists are disabled.
Two selected accounts continue to work at 2. Choosing 3 with fewer selected
accounts preserves the previous Sync playlist and reports the missing setup.
Sync still ignores personal category selection, clear rate and track limits.
It matches exact hash, characteristic and difficulty, groups charts under each
song, and includes every qualifying chart. NF counts as posted; local attempts,
failed/partial runs and inferred imports do not. AccSaber imports from other
services can lag; its own posted-score response is authoritative.

Complete histories and ranked catalogs are required from every participant.
Failed/incomplete responses preserve the previous Sync playlist. A successful
empty result writes an empty playlist. The fuller AccSaber map endpoint is used
because the compact ranked-difficulty response omits chart characteristics.

## Clans and artwork

AccSaber clan playlists are deferred. AccSaber allows one clan at a time; its
ranking combines member skill with seasonal standing. Clan missions and wars
provide contributions. War replays can damage opponents without becoming PBs,
so BeatLeader's weighted map-capture calculation is not an appropriate model.
See [membership](https://www.accsaber.com/wiki/clans),
[standing](https://www.accsaber.com/wiki/clan-standing), and
[war combat](https://github.com/accsaber/accsaber-reloaded-frontend/blob/705ecb155f431328ac982bb1b94deeaac7f44321/src/wiki/pages/WarCombatPage.vue).

The supplied Improve, Not Played and Simple Mode images are used unchanged for
the three personal playlist types. The new Sync cover replaces the previous
one. The supplied clan cover is retained for a later clan implementation and
is not included in current package definitions. No additional artwork is needed.

## Validation status

The 0.1.28 preview release targets Quest Beat Saber 1.40.8_7379 and Windows
PCVR 1.40.8/1.44.1. The focused [testing checklist](../TESTING-0.1.28.md) covers installation,
new defaults, AccSaber categories/charts, Sync and failure preservation.

The source-only phase passed six native suites, 663 managed/source-binding
checks, C# syntax checks and supplied-artwork comparisons. For packaging, the
actual PC plugin/engine passed compiled bindings, native interop and verified
HTTPS checks; the Quest sources are compiled with Android NDK r26d.

Live API access succeeded outside the network sandbox on November 12, 2026.
The compiled PC engine fetched the complete 507-map ranked catalog and 333-score
history for one public profile, produced 16 AP Gain recommendations and built
independent True/Standard/Tech category fits (59/148/125 eligible observations).
The refresh reported about 7.2 seconds on this PC. This is one public snapshot,
not a headset timing or validation of every profile. Twelve captured unmodified
AP values and the live curve are retained in the reduced reference fixture.

On November 13, 2026, the maintainer reported the Quest HTTPS-fix QMOD working
as intended in their usual workflow. The release QMOD has identical bytes.
This is not exhaustive validation of AccSaber categories or every account.
Current PCVR builds, account-specific behavior and the full acceptance checklist
remain for testers. AccSaber clan support remains deferred.
