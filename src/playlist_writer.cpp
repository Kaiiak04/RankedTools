#include "platform.hpp"
#include "playlist_writer.hpp"
#include "playlist_image.hpp"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cstdio>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_set>
#include <vector>

namespace rankedpractice {
namespace {

constexpr const char* kClanPlaylistFilePrefix =
    "RankedPractice_BeatLeader_Clan_ToConquer";

void AddString(rapidjson::Writer<rapidjson::StringBuffer>& writer,
               const char* key,
               const std::string& value) {
    writer.Key(key);
    writer.String(value.c_str(), static_cast<rapidjson::SizeType>(value.size()));
}

std::string PlaylistImage(const std::string& fileName) {
    const std::filesystem::path path = RuntimePaths().icons / fileName;
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return EncodePlaylistImage(bytes, "image/png");
}

bool IsValidSongHash(const std::string& hash) {
    return hash.size() == 40 && std::all_of(hash.begin(), hash.end(),
        [](unsigned char c) { return std::isxdigit(c) != 0; });
}

bool WritePlaylistFile(const std::string& title,
                       const std::string& fileName,
                       const std::string& image,
                       const std::vector<MapEntry>& entries,
                       std::string& error,
                       const std::string& description = "Generated in Beat Saber from public leaderboard data.") {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    AddString(writer, "playlistTitle", title);
    AddString(writer, "playlistAuthor", "RankedTools");
    AddString(writer, "playlistDescription", description);
    writer.Key("songs");
    writer.StartArray();
    for (const auto& entry : entries) {
        // PlaylistCore rejects songs with an empty/non-BeatSaver hash. One bad
        // row invalidates the entire playlist there, so never serialize it.
        if (!IsValidSongHash(entry.hash)) continue;
        writer.StartObject();
        AddString(writer, "hash", entry.hash);
        AddString(writer, "songName", entry.songName);
        AddString(writer, "songSubName", entry.songSubName);
        AddString(writer, "songAuthorName", entry.songAuthorName);
        AddString(writer, "levelAuthorName", entry.levelAuthorName);
        AddString(writer, "levelid", std::string("custom_level_") + entry.hash);
        writer.Key("difficulties");
        writer.StartArray();
        for (const auto& difficulty : entry.difficulties) {
            writer.StartObject();
            AddString(writer, "characteristic", difficulty.characteristic);
            AddString(writer, "name", difficulty.name);
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();
    }
    writer.EndArray();
    AddString(writer, "image", image);
    writer.Key("customData");
    writer.StartObject();
    writer.Key("syncURL");
    writer.Null();
    writer.EndObject();
    writer.EndObject();

    const auto& directory = RuntimePaths().playlists;
    if (!std::filesystem::exists(directory.parent_path())) {
        error = "PlaylistManager is not installed, or its playlist folder is unavailable.";
        return false;
    }
    std::filesystem::create_directories(directory);
    const auto finalPath = directory / fileName;
    auto tempPath = finalPath;
    tempPath += ".tmp";
    {
        std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "Could not open PlaylistManager's playlist folder for writing.";
            return false;
        }
        output.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
        output.flush();
        if (!output) {
            error = "Could not finish writing the generated playlist.";
            return false;
        }
    }
    std::error_code ec;
    ReplaceFile(tempPath, finalPath, ec);
    if (ec) {
        error = "Playlist was written to a temporary file, but could not be installed: " + ec.message();
        return false;
    }
    return true;
}

} // namespace

bool WritePlaylist(const std::string& service,
                   ListKind kind,
                   const std::vector<MapEntry>& entries,
                   std::string& error) {
    try {
        const auto icon = service == "ScoreSaber" ? "scoresaber_" : "beatleader_";
        const auto image = PlaylistImage(std::string(icon) +
            (kind == ListKind::Combined ? "ppgain.png" :
             kind == ListKind::NotPlayed ? "notplayed.png" : "toimprove.png"));
        return WritePlaylistFile(PlaylistTitle(service, kind), PlaylistFileName(service, kind),
                                 image, entries, error);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool WriteClanPlaylist(const std::string& clanTag,
                       bool perClan,
                       const std::vector<MapEntry>& entries,
                       std::string& error,
                       const std::string& clanImage,
                       bool hypothetical, bool legacy) {
    try {
        return WritePlaylistFile(ClanPlaylistTitle(clanTag + (legacy ? "; legacy" : "") + (hypothetical ? "; what-if primary clan" : "")), ClanPlaylistFileName(clanTag, perClan),
                                 clanImage.empty() ? PlaylistImage("beatleader_clan_toconquer.png") : clanImage,
                                 entries, error, legacy
                                     ? (hypothetical
                                         ? "Legacy clan-gap order for a what-if primary clan. Current clan totals determine the order; eligible personal PBs filter out charts without a predicted improvement. Automatic maximum stars; no lower star limit."
                                         : "Legacy clan-gap order with an automatic maximum star cap and personal PB filtering. Includes unplayed charts and predicted improvements, with no lower star limit.")
                                     : hypothetical
                                     ? "Estimated captures and progress if this clan becomes your primary clan. Existing PBs are transferred before measuring gameplay gains. Targeted verification may miss opportunities."
                                     : "Personalized estimated captures first, then useful weighted clan PP progress. Targeted verification may miss opportunities.");
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool PruneClanPlaylists(const std::vector<std::string>& expectedFileNames,
                        std::string& error) {
    try {
        const auto& directory = RuntimePaths().playlists;
        if (!std::filesystem::exists(directory)) return true;

        const std::unordered_set<std::string> expected(expectedFileNames.begin(),
                                                       expectedFileNames.end());
        for (const auto& item : std::filesystem::directory_iterator(directory)) {
            if (!item.is_regular_file()) continue;
            const auto name = item.path().filename().string();
            if (name.rfind(kClanPlaylistFilePrefix, 0) != 0 ||
                item.path().extension() != ".bplist" || expected.contains(name)) continue;

            std::error_code ec;
            std::filesystem::remove(item.path(), ec);
            if (ec) {
                error = "Could not remove an obsolete generated clan playlist: " + ec.message();
                return false;
            }
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool PruneLeaderboardPlaylists(const std::string& service,
                               const std::vector<std::string>& expectedFileNames,
                               std::string& error) {
    try {
        const std::unordered_set<std::string> expected(expectedFileNames.begin(), expectedFileNames.end());
        for (auto kind : {ListKind::NotPlayed, ListKind::ToImprove, ListKind::Combined}) {
            const auto name = PlaylistFileName(service, kind);
            if (expected.contains(name)) continue;
            const auto path = RuntimePaths().playlists / name;
            std::error_code ec;
            // remove() also succeeds if the file no longer exists.
            std::filesystem::remove(path, ec);
            if (ec) { error = "Could not remove an obsolete generated leaderboard playlist: " + ec.message(); return false; }
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what(); return false;
    }
}

} // namespace rankedpractice
