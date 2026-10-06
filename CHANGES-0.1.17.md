# RankedTools 0.1.17

Adds an **Ability chart** view to the RankedTools main-menu tab. Switch between **Song details** and **Ability chart**, then choose **BeatLeader** or **ScoreSaber**. The chart uses each enabled service's complete fetched profile rather than the playlist's track limit, and works in either playlist layout.

Gray dots show the eligible best scores used in the accuracy fit. Older scores are dimmer. The cyan line is the same recency-weighted monotonic model used for recommendations, including exact fitted star knots and the endpoint/tail behavior. Shading and dashed lines mark regions outside the played star range. Axes show accuracy (%) and effective stars; the header shows score count and observed range.

This estimates accuracy on a scored run, not clear probability. Failed attempts and No Fail runs do not become accuracy observations. Empty or entirely excluded histories show an explanation instead of displaying the generic prediction fallback as a personal model. Disabled services, missing player IDs, and profile-loading errors each have explicit states. Charts are reset on refresh so a changed player ID cannot retain the previous player's graph.

Charts reuse fetched profile scores without additional HTTP requests. Model preparation happens on the refresh worker; Unity texture upload and layout updates run on the main thread. The current texture is reused while navigating the same chart, and replaced textures/sprites are destroyed.

Validation: 1,958 recommendation/history/chart checks and 27 playlist-writer checks passed. The chart checks cover training eligibility, deduplication, modifier-adjusted stars, exact model predictions, monotonicity, outlier pooling, empty/single-score histories, and bitmap output. A PNG exported by the production renderer using the saved grug profile was inspected. Quest compilation and QMOD archive verification passed. Headset rendering and interaction remain untested.
