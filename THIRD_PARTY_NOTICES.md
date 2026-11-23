# Third-party notices

RankedTools source is licensed under GPL-3.0-only; see [LICENSE](LICENSE). Third-party components retain their own licenses. The QPM dependency versions and upstream source branches are recorded in [qpm.shared.json](qpm.shared.json). Dependency sources and machine-local analysis files are not checked into this repository.

| Component | Version | License / source |
|---|---|---|
| Quest mod template | Original project template | [Unlicense](licenses/quest-mod-template-Unlicense.txt), [source](https://github.com/Lauriethefish/quest-mod-template) |
| config-utils | 2.0.3 | GPLv3; full text in [LICENSE](LICENSE), [upstream source](https://github.com/darknight1050/config-utils/tree/version/v2_0_3) |
| beatsaber-hook | 6.4.2 | [MIT notice](licenses/beatsaber-hook-MIT.txt), [source](https://github.com/QuestPackageManager/beatsaber-hook) |
| custom-types | 0.18.4 | [MIT notice](licenses/custom-types-MIT.txt), [source](https://github.com/QuestPackageManager/Il2CppQuestTypePatching) |
| scotland2 | 0.1.7 | [MIT notice](licenses/scotland2-MIT.txt), [source](https://github.com/sc2ad/scotland2) |
| fmt | 11.0.2 | [MIT notice](licenses/fmt-MIT.txt), [source](https://github.com/fmtlib/fmt/tree/11.0.2) |
| RapidJSON | Supplied by beatsaber-hook | [Upstream notices](licenses/RapidJSON.txt), [source](https://github.com/Tencent/rapidjson) |
| BeatLeader PPv4 calculation | C++ adaptation of website commit 606b7f9 | GPLv3; full text in [LICENSE](LICENSE), [upstream source](https://github.com/BeatLeader/beatleader-website/blob/606b7f9d33fec7dcf2427cea1d1151b74a5ec006/src/utils/beatleader/rebalanced-pp.js) |
| utf8cpp | Supplied by Paper / beatsaber-hook | [Boost Software License notice](licenses/utf8cpp.txt), [source](https://github.com/nemtrif/utfcpp) |
| curl | 8.23.0, built from source | [curl license](licenses/curl.txt), [source](https://github.com/curl/curl/tree/curl-8_23_0) |
| Mbed TLS | 3.6.7, built from source | Apache-2.0 OR GPL-2.0-or-later; used under Apache-2.0. [Full notices](licenses/Mbed-TLS.txt), [source](https://github.com/Mbed-TLS/mbedtls/tree/mbedtls-3.6.7) |

The remaining QPM packages are fetched for building/linking and keep their upstream terms: [BSML](https://github.com/bsq-ports/Quest-BSML), [Paper](https://github.com/Fernthedev/paperlog), [conditional-dependencies](https://github.com/RedBrumbler/ConditionalDependencies), [rapidjson-macros](https://github.com/Metalit/RapidjsonMacros), and the versioned IL2CPP/generated type headers identified by the QPM lockfile. These are not relicensed as RankedTools code. This notice is not a statement that every dependency combination is redistributable.

The bundled `cacert.pem` contains public Mozilla CA root certificates extracted with curl's mk-ca-bundle tooling. Its provenance, date and source links are retained in that file.

The eight playlist covers are original hand-drawn artwork supplied by the RankedTools project owner. They are included under this project's GPLv3 license. See [assets/ARTWORK.md](assets/ARTWORK.md). BeatLeader/ScoreSaber names identify the services the mod supports; this project does not claim endorsement by them. Clan icons are retrieved at runtime from BeatLeader and are not bundled in the repository.

## HTTPS build and binary distribution

Starting with 0.1.23, `cmake/https.cmake` builds curl 8.23.0 and Mbed TLS 3.6.7 from release archives pinned by SHA-256. The final mod links those targets. The older QPM libcurl 8.5.0 entry remains for its public headers and standalone fake-client tests; its prebuilt archive is explicitly removed from the final link list in `cmake/qpm.cmake`. Neither that archive nor its embedded OpenSSL 1.1.1 code is part of the released mod. The historical wrapper and OpenSSL license files remain in `licenses` for provenance.

The QMOD includes this notice, the project license, and the `licenses` directory. Matching source is in the release's tagged repository archive; pinned HTTPS source archives are also included with the release. Other dependency versions and source locations remain recorded in `qpm.shared.json`. The release remains a Quest preview because on-headset validation is pending.

## PCVR preview

The Windows native engine uses the same curl 8.23.0 / Mbed TLS 3.6.7 source archives and the RapidJSON 1.1.0 source revision `f54b0e47a08782a6131cc3d60f94d038fa6e0a51`. Its compiler and TLS versions are recorded in each package's `build-info.json`. MSVC builds use the static C++ runtime. Portable llvm-mingw builds also include that toolchain's LLVM/runtime notices.

The managed plugin compiles compatibility helpers from [ScoreSaber Legato](https://github.com/ScoreSaber/legato), package `LegatoBS` 0.1.1, under GPL-3.0-only; its license is included in the ZIP. Build-only .NET packages are pinned in `pcvr/RankedTools.Pcvr/packages.lock.json`.

BSIPA, PC BSML, SiraUtil, SongCore, BeatSaberPlaylistsLib, and any playlist browser retain their upstream terms and are installed separately. The exact build dependency archives and hashes are in `pcvr/dependencies.lock.json`; optional playlist-browser downloads are in `pcvr/optional-dependencies.lock.json`. Public compile-only game references are obtained from [beat-forge/beatsaber-stripped](https://github.com/beat-forge/beatsaber-stripped) at the commits in `pcvr/targets.json`. Game reference assemblies and other mods are excluded from the RankedTools ZIPs. Matching RankedTools source is provided by the release tag's repository archive.
