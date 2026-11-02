#pragma once

#include <atomic>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef GetObject
#undef GetObject
#endif
#endif

#ifndef RANKEDTOOLS_ATTEMPT_DIRECTORY
#define RANKEDTOOLS_ATTEMPT_DIRECTORY "/sdcard/ModData/com.beatgames.beatsaber/Mods/RankedPractice/attempts"
#endif
#ifndef RANKEDTOOLS_CLAN_CURSOR_DIRECTORY
#define RANKEDTOOLS_CLAN_CURSOR_DIRECTORY "/sdcard/ModData/com.beatgames.beatsaber/Mods/RankedPractice/clan-search"
#endif
#ifndef RANKEDTOOLS_PLAYLIST_DIRECTORY
#define RANKEDTOOLS_PLAYLIST_DIRECTORY "/sdcard/ModData/com.beatgames.beatsaber/Mods/PlaylistManager/Playlists"
#endif
#ifndef RANKEDTOOLS_ICON_DIRECTORY
#define RANKEDTOOLS_ICON_DIRECTORY "/sdcard/ModData/com.beatgames.beatsaber/Mods/RankedPractice"
#endif
#ifndef RANKEDTOOLS_CA_BUNDLE_PATH
#define RANKEDTOOLS_CA_BUNDLE_PATH "/sdcard/ModData/com.beatgames.beatsaber/Mods/RankedPractice/cacert.pem"
#endif

namespace rankedpractice {

// Configure once, before starting workers. Quest and standalone regression tests
// retain their existing defaults; the PC host supplies absolute UTF-8 paths.
struct PlatformPaths {
    std::filesystem::path attempts{RANKEDTOOLS_ATTEMPT_DIRECTORY};
    std::filesystem::path clanSearch{RANKEDTOOLS_CLAN_CURSOR_DIRECTORY};
    std::filesystem::path playlists{RANKEDTOOLS_PLAYLIST_DIRECTORY};
    std::filesystem::path icons{RANKEDTOOLS_ICON_DIRECTORY};
    std::filesystem::path caBundle{RANKEDTOOLS_CA_BUNDLE_PATH};
    std::string caCertificates; // PC loads PEM through a Unicode-aware filesystem path.
    std::string userAgent{"RankedTools/0.1.24 (Beat Saber Quest mod)"};
};
inline PlatformPaths &RuntimePaths() {
    static PlatformPaths paths;
    return paths;
}
inline std::atomic<bool> &RuntimeCancelled() {
    static std::atomic<bool> cancelled{false};
    return cancelled;
}
inline std::filesystem::path Utf8Path(const std::string &value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}
inline std::string PathUtf8(const std::filesystem::path &path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}

// Neither implementation removes the previous destination on failure. This is
// essential on Windows, where std::filesystem::rename cannot replace a file.
inline void ReplaceFile(const std::filesystem::path &temporary, const std::filesystem::path &destination,
                        std::error_code &error) {
#ifdef _WIN32
    if (MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        error.clear();
    else
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
    std::filesystem::rename(temporary, destination, error);
#endif
}
inline std::tm UtcTime(std::time_t value) {
    std::tm result{};
#ifdef _WIN32
    gmtime_s(&result, &value);
#else
    gmtime_r(&value, &result);
#endif
    return result;
}
} // namespace rankedpractice
