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
| utf8cpp | Supplied by Paper / beatsaber-hook | [Boost Software License notice](licenses/utf8cpp.txt), [source](https://github.com/nemtrif/utfcpp) |
| Android curl build wrapper | QPM libcurl 8.5.0 package | [MIT notice](licenses/curl-android-wrapper-MIT.txt), [source](https://github.com/darknight1050/openssl-curl-android) |
| curl | 8.5.0 | [curl license](licenses/curl.txt), [source](https://github.com/curl/curl/tree/curl-8_5_0) |
| OpenSSL | 1.1.1 family, present in the prebuilt curl archive | [OpenSSL / SSLeay license](licenses/OpenSSL-1.1.1.txt), [source](https://github.com/openssl/openssl/tree/OpenSSL_1_1_1w) |

The remaining QPM packages are fetched for building/linking and keep their upstream terms: [BSML](https://github.com/bsq-ports/Quest-BSML), [Paper](https://github.com/Fernthedev/paperlog), [conditional-dependencies](https://github.com/RedBrumbler/ConditionalDependencies), [rapidjson-macros](https://github.com/Metalit/RapidjsonMacros), and the versioned IL2CPP/generated type headers identified by the QPM lockfile. These are not relicensed as RankedTools code. This notice is not a statement that every dependency combination is redistributable.

The bundled `cacert.pem` contains public Mozilla CA root certificates extracted with curl's mk-ca-bundle tooling. Its provenance, date and source links are retained in that file.

The seven playlist covers are original hand-drawn artwork supplied by the RankedTools project owner. They are included under this project's GPLv3 license. See [assets/ARTWORK.md](assets/ARTWORK.md). BeatLeader/ScoreSaber names identify the services the mod supports; this project does not claim endorsement by them. Clan icons are retrieved at runtime from BeatLeader and are not bundled in the repository.

## Binary distribution status

No QMOD binary is published with this initial source release. Inspection of the current prebuilt `libcurl.a` found OpenSSL 1.1.1w / 1.1.1 development version strings. That older TLS licensing needs a GPL-compatible replacement or appropriate permission before distributing the combined GPLv3 binary. Adding these notices alone does not resolve that compatibility issue. The CI workflow builds and tests the code, but does not upload compiled artifacts.
