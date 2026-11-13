# AccSaber source-contract fixtures

These are synthetic fixtures, not live responses. Generate them with
`node tests/generate_accsaber_fixtures.mjs`. They exercise the public DTOs and
Spring page envelopes at backend commit `7a97d2424a6f89d48245335645e86c1ee65966bf`.
The deliberately small curve allows independently checkable AP values; its
shape is not intended to represent the live AccSaber ranking curve.

Primary references: `controller/map/MapController.java`,
`controller/user/UserController.java`, `controller/user/LeaderboardController.java`,
`service/score/APCalculationService.java`, and `service/score/ScoreService.java`
under https://github.com/accsaber/accsaber-reloaded-backend/tree/7a97d2424a6f89d48245335645e86c1ee65966bf/src/main/java/com/accsaber/backend.

`live-ap-reference.json` is a separate live capture from November 12, 2026:
the published 57-point score curve and twelve unmodified AP examples, reduced
to the fields needed for comparison. Its `sources` and capture timestamp record
provenance. The synthetic fixture generator does not overwrite this live file.
