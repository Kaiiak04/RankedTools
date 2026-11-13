# RankedTools 0.1.26 — Sync history fix

- Fixed Sync aborting when either account has scores on built-in OST songs. Those records use official level IDs instead of custom-map hashes and are now excluded from the ranked custom-chart comparison.
- Kept exact-chart matching, all characteristics, unlimited matches, posted No Fail score handling and preservation on incomplete input unchanged.
- Added reduced real-response fixtures and checks for mixed custom/OST histories, all-OST pages, later custom-score pages and malformed custom hashes.
- Shared fix for Quest and Windows x64 PCVR 1.40.8/1.44.1. Both game versions still need in-game validation; the 1.44.1 build continues to use the agreed 1.42 API-family references.
