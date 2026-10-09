# RankedTools 0.1.21

Packages RankedTools as a normal Scotland2 mod using `lateModFiles`, rather than as an Early Mod using `modFiles`. The binary now opens during Unity initialization. The existing `late_load()` entry point continues to initialize configuration, gameplay hooks and menus.

This changes loading phase only. Recommendation behavior and required dependencies are unchanged. Startup, menu registration and local attempt recording still need verification on a headset after reinstalling the updated QMOD.

Validation: Quest arm64 build and QMOD manifest schema validation passed. The packaged manifest lists the binary only under `lateModFiles`; version 0.1.21, Scotland2 loader, Beat Saber 1.40.8_7379 target, all 10 archive entries, current binary hash and bundled file hashes were verified. The binary exports `setup` and `late_load`. Generic and versioned QMOD copies match.
