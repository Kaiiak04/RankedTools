#include <iostream>
#include <stdexcept>
// Exercise the real writer and cleanup against a workspace-local test directory.
#include "../src/playlist_writer.cpp"
#include "../src/playlist_image.cpp"
#include <rapidjson/document.h>

using namespace rankedpractice;
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error( \
    std::string("Line ") + std::to_string(__LINE__) + ": " #condition); } while (false)

int main() {
    RuntimePaths().playlists = "tests/playlist-sandbox";
    RuntimePaths().icons = "tests/playlist-icon-sandbox";
    const auto& directory = RuntimePaths().playlists;
    const std::vector<std::string> protectedFiles{
        "RankedPractice_ScoreSaber_NotPlayed.bplist", "RankedPractice_BeatLeader_Clan_ToConquer.bplist",
        "MyPlaylist.bplist", "RankedPractice_BeatLeader_NotPlayed.bplist.bak"
    };
    try {
        CHECK(!std::filesystem::exists(directory));
        std::filesystem::create_directories(directory);
        for (const auto& name : protectedFiles) std::ofstream(directory / name) << "keep";
        MapEntry entry; entry.hash = std::string(40, 'a'); entry.songName = "Test";
        entry.difficulties = {{"Standard", "Expert", 7}};
        std::string error;
        for (auto kind : {ListKind::NotPlayed, ListKind::ToImprove, ListKind::Combined}) {
            CHECK(WritePlaylist("BeatLeader", kind, {entry}, error));
        }
        CHECK(PruneLeaderboardPlaylists("BeatLeader", {PlaylistFileName("BeatLeader", ListKind::Combined)}, error));
        CHECK(!std::filesystem::exists(directory / PlaylistFileName("BeatLeader", ListKind::NotPlayed)));
        CHECK(!std::filesystem::exists(directory / PlaylistFileName("BeatLeader", ListKind::ToImprove)));
        const auto combinedPath = directory / PlaylistFileName("BeatLeader", ListKind::Combined);
        CHECK(std::filesystem::exists(combinedPath));
        std::ifstream input(combinedPath);
        const std::string json{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        input.close();
        rapidjson::Document playlist; playlist.Parse(json.c_str());
        CHECK(!playlist.HasParseError());
        CHECK(std::string(playlist["playlistTitle"].GetString()) == "BeatLeader - PP Gain");
        CHECK(playlist["songs"].Size() == 1);
        CHECK(std::string(playlist["songs"][0]["difficulties"][0]["name"].GetString()) == "Expert");
        for (auto kind : {ListKind::NotPlayed, ListKind::ToImprove}) CHECK(WritePlaylist("BeatLeader", kind, {entry}, error));
        CHECK(PruneLeaderboardPlaylists("BeatLeader", {
              PlaylistFileName("BeatLeader", ListKind::NotPlayed), PlaylistFileName("BeatLeader", ListKind::ToImprove)}, error));
        CHECK(!std::filesystem::exists(combinedPath));
        for (auto kind : {ListKind::NotPlayed, ListKind::ToImprove}) CHECK(std::filesystem::exists(directory / PlaylistFileName("BeatLeader", kind)));
        for (const auto& name : protectedFiles) {
            CHECK(std::filesystem::exists(directory / name));
            std::ifstream file(directory / name); std::string value; file >> value; CHECK(value == "keep");
        }
        CHECK(PruneLeaderboardPlaylists("BeatLeader", {
              PlaylistFileName("BeatLeader", ListKind::NotPlayed), PlaylistFileName("BeatLeader", ListKind::ToImprove)}, error));
        // Clean up only the enumerated fixtures in this workspace-local sandbox.
        std::ifstream syncInput("sync.png", std::ios::binary);
        const std::string syncPng{std::istreambuf_iterator<char>(syncInput), std::istreambuf_iterator<char>()};
        syncInput.close();
        CHECK(!syncPng.empty());
        const auto syncPath = directory / PlaylistFileName("Sync", ListKind::Sync);
        // The actual packaged artwork, all 205 songs and both selected chart modes.
        std::filesystem::create_directories(RuntimePaths().icons);
        { std::ofstream cover(RuntimePaths().icons / "sync.png", std::ios::binary); cover << syncPng; }
        auto syncEntry = entry;
        syncEntry.difficulties.push_back({"OneSaber", "Hard", 0});
        CHECK(WritePlaylist("Sync", ListKind::Sync, std::vector<MapEntry>(205, syncEntry), error));
        {
            std::ifstream file(syncPath);
            const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            rapidjson::Document doc; doc.Parse(text.c_str());
            CHECK(!doc.HasParseError() && doc["songs"].Size() == 205);
            CHECK(std::string(doc["playlistTitle"].GetString()) == "Sync");
            CHECK(std::string(doc["image"].GetString()) == EncodePlaylistImage(syncPng, "image/png"));
            CHECK(doc["songs"][0]["difficulties"].Size() == 2);
        }
        CHECK(PruneLeaderboardPlaylists("BeatLeader", {}, error));
        CHECK(PruneLeaderboardPlaylists("ScoreSaber", {}, error));
        CHECK(std::filesystem::exists(syncPath));
        auto invalidSync = syncEntry; invalidSync.hash = "invalid";
        auto syncBytes = [&] {
            std::ifstream file(syncPath, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        };
        const auto beforeFailure = syncBytes();
        CHECK(!WritePlaylist("Sync", ListKind::Sync, {invalidSync}, error));
        CHECK(syncBytes() == beforeFailure);
        CHECK(WritePlaylist("Sync", ListKind::Sync, {}, error)); // A successful empty result replaces old entries.
        std::filesystem::remove(syncPath);
        std::filesystem::remove(RuntimePaths().icons / "sync.png");
        std::filesystem::remove(RuntimePaths().icons);
        std::ifstream pngInput("tests/fixtures/clan-icon-live.png", std::ios::binary);
        const std::string png{std::istreambuf_iterator<char>(pngInput), std::istreambuf_iterator<char>()};
        CHECK(!png.empty());
        const auto& iconDirectory = RuntimePaths().icons;
        CHECK(!std::filesystem::exists(iconDirectory));
        std::filesystem::create_directories(iconDirectory);
        const auto fallbackIcon = iconDirectory / "beatleader_clan_toconquer.png";
        { std::ofstream fallback(fallbackIcon, std::ios::binary); fallback << png; }
        auto readImage = [&](const std::string& tag, bool perClan) {
            std::ifstream file(directory / ClanPlaylistFileName(tag, perClan));
            const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            rapidjson::Document document; document.Parse(text.c_str());
            CHECK(!document.HasParseError() && document["songs"].Size() == 1);
            return std::string(document["image"].GetString());
        };
        const auto imageA = DownloadedPlaylistImage(png);
        const auto imageB = EncodePlaylistImage("different image payload", "image/png");
        CHECK(WriteClanPlaylist("A", true, {entry}, error, imageA));
        CHECK(WriteClanPlaylist("B", true, {entry}, error, imageB));
        CHECK(readImage("A", true) == imageA && readImage("B", true) == imageB);
        CHECK(WriteClanPlaylist("B", false, {entry}, error, imageB));
        CHECK(readImage("B", false) == imageB);
        CHECK(WriteClanPlaylist("B", true, {entry}, error, imageB, true));
        {
            std::ifstream file(directory / ClanPlaylistFileName("B", true));
            const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            rapidjson::Document doc; doc.Parse(text.c_str());
            CHECK(!doc.HasParseError());
            CHECK(std::string(doc["playlistTitle"].GetString()).find("what-if primary clan") != std::string::npos);
            CHECK(std::string(doc["playlistDescription"].GetString()).find("Existing PBs are transferred") != std::string::npos);
            CHECK(readImage("B", true) == imageB);
        }
        CHECK(WriteClanPlaylist("A", true, {entry}, error));
        CHECK(readImage("A", true) == imageA); // Actual packaged PNG is used when no downloaded cover exists.
        CHECK(WriteClanPlaylist("B", true, {entry}, error, imageB, true, true));
        {
            std::ifstream file(directory / ClanPlaylistFileName("B",true));
            const std::string text{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
            rapidjson::Document doc; doc.Parse(text.c_str());
            CHECK(!doc.HasParseError() && doc["songs"].Size()==1);
            const std::string title=doc["playlistTitle"].GetString(), description=doc["playlistDescription"].GetString();
            CHECK(title.find("legacy")!=std::string::npos && title.find("what-if primary clan")!=std::string::npos);
            CHECK(description.find("automatic maximum")!=std::string::npos || description.find("Automatic maximum")!=std::string::npos);
            CHECK(readImage("B",true)==imageB);
        }
        CHECK(WriteClanPlaylist("B",true,{entry},error,imageB,true,false));
        {
            std::ifstream file(directory / ClanPlaylistFileName("B",true));
            const std::string text{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
            rapidjson::Document doc; doc.Parse(text.c_str());
            CHECK(std::string(doc["playlistTitle"].GetString()).find("legacy")==std::string::npos);
            CHECK(std::string(doc["playlistDescription"].GetString()).find("Existing PBs are transferred")!=std::string::npos);
        }
        std::filesystem::remove(directory / ClanPlaylistFileName("A", true));
        std::filesystem::remove(directory / ClanPlaylistFileName("B", true));
        std::filesystem::remove(fallbackIcon);
        std::filesystem::remove(iconDirectory);
        for (auto kind : {ListKind::NotPlayed, ListKind::ToImprove}) std::filesystem::remove(directory / PlaylistFileName("BeatLeader", kind));
        for (const auto& name : protectedFiles) std::filesystem::remove(directory / name);
        std::filesystem::remove(directory);
        std::cout << "All " << checks << " playlist writer checks passed: playlist serialization, layout preservation, per-clan covers and fallback.\n";
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n'; return 1;
    }
}
