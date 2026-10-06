# RankedTools 0.1.15

Accuracy predictions now use recency-weighted monotonic regression instead of a quadratic fit. Increasing a chart's star rating can only maintain or reduce its predicted accuracy. Both leaderboards, Not Played, To Improve, Simple Mode and automatic target-star selection use this model.

## Fitting and prediction

- The existing ranked Standard best-score selection, highest-PP chart deduplication, No Fail exclusion and 55%-100% training accuracy range are preserved. Effective modifier stars are preferred when available. Nonfinite values and zero-weight observations are ignored.
- Recency weights remain `exp(-ageDays / 365)`, with a weight of 0.65 for a missing timestamp and 1 for a future timestamp. PP is not a regression weight.
- Observations are sorted by stars. Equal star ratings are combined into one recency-weighted mean, retaining their total weight.
- The pool-adjacent-violators algorithm minimizes weighted squared accuracy errors under the constraint that accuracy cannot increase with stars. Adjacent blocks that violate this constraint are merged into a weighted mean, repeating backward as necessary. All observed star ratings in a merged block receive that same fitted accuracy.
- Between observed star ratings, predictions use linear interpolation. This preserves the non-increasing relationship and creates flat sections where scores were pooled.
- Outside the observed range, the nearest endpoint prediction is held for one extra star. Beyond that buffer, the estimate decreases by 1.2 percentage points per additional star on the harder side, or increases by 0.6 points per additional star on the easier side. Predictions remain bounded to 55%-99.9%.
- One distinct star rating produces a flat prediction inside the endpoint buffer. Two distinct ratings can now support interpolation; the old quadratic model required at least three scores. With no eligible observations, the default remains 85%.

Actual failed attempts and non-personal-best clears continue to affect the separate clearability model. They do not train accuracy predictions. The fitted accuracy represents leaderboard best-score performance, not average accuracy across attempts. Recommendation order and automatic search bands may change after refreshing.

## Validation

All 1,156 recommendation/history checks and 27 playlist-writer checks passed. New cases cover weighted and cascading pools, equal-star observations, interpolation, sparse profiles, recency, endpoint behavior, bounds, exclusions and both recommendation modes on both leaderboards. A noisy-profile sweep checks that predictions are finite, bounded and non-increasing across the fitted range and tails. The Quest arm64 build passed. Headset behavior has not been tested.
