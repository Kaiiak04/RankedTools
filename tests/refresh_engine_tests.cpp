// Exercise production orchestration with injected provider/storage boundaries.
#include "refresh_engine.hpp"
#include "platform.hpp"
#include <atomic>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>
using namespace rankedpractice;
namespace {
std::mutex mutex;
std::vector<std::string> writes, prunes;
bool failFetch{}, failWrite{}, failClan{}, failMember{}, emptyMembership{};
bool failSyncOnce{}, throwSync{};
int checks{}, fetches{};
bool capturedBeatLeaderModifiers{}, capturedClanModifiers{};
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(x))                                                                                                      \
            throw std::runtime_error(#x);                                                                              \
    } while (false)
void Reset() {
    writes.clear();
    prunes.clear();
    failFetch = failWrite = failClan = failMember = emptyMembership = false;
    failSyncOnce = throwSync = false;
    fetches = 0;
    capturedBeatLeaderModifiers = false;
    capturedClanModifiers = false;
}
} // namespace
namespace rankedpractice {
HttpStatistics GetHttpStatistics() { return {}; }
std::string PlaylistFileName(const std::string &service, ListKind kind) {
    return service + std::to_string(static_cast<int>(kind));
}
std::string PlaylistTitle(const std::string &service, ListKind kind) { return PlaylistFileName(service, kind); }
std::string ClanPlaylistFileName(const std::string &clan, bool all) { return clan + (all ? "_all" : "_main"); }
bool FetchRecommendations(const std::string &service, const std::string &, ListKind kind, int, std::vector<MapEntry> &entries,
                          std::string &error, double, StarRange *, ScoreHistory *history, bool modifiers) {
    std::lock_guard lock(mutex);
    ++fetches;
    if (service == "BeatLeader") capturedBeatLeaderModifiers = modifiers;
    history->loaded = true;
    if (failFetch && kind == ListKind::ToImprove) {
        error = "incomplete pages";
        return false;
    }
    entries.push_back({});
    return true;
}
bool WritePlaylist(const std::string &service, ListKind kind, const std::vector<MapEntry> &, std::string &error) {
    if (failWrite) {
        error = "disk full";
        return false;
    }
    std::lock_guard lock(mutex);
    writes.push_back(PlaylistFileName(service, kind));
    return true;
}
bool PruneLeaderboardPlaylists(const std::string &service, const std::vector<std::string> &, std::string &) {
    std::lock_guard lock(mutex);
    prunes.push_back(service);
    return true;
}
bool FetchClanToConquer(const std::string &, bool, int, std::vector<ClanPlaylistRecommendations> &result,
                        std::string &error, double, ScoreHistory *, bool legacy, bool modifiers) {
    capturedClanModifiers = modifiers;
    if (failMember) {
        error = "membership unavailable";
        return false;
    }
    if (!emptyMembership) {
        ClanPlaylistRecommendations a;
        a.clanTag = "A";
        a.legacy = legacy;
        a.cursorReady = true;
        a.entries.push_back({});
        ClanPlaylistRecommendations b;
        b.clanTag = "B";
        b.legacy = legacy;
        if (failClan)
            b.error = "verification unavailable";
        result = {a, b};
    }
    return true;
}
bool WriteClanPlaylist(const std::string &tag, bool, const std::vector<MapEntry> &, std::string &, const std::string &,
                       bool, bool) {
    writes.push_back(tag);
    return true;
}
bool PruneClanPlaylists(const std::vector<std::string> &, std::string &) {
    prunes.push_back("clans");
    return true;
}
bool SaveClanSearchState(const std::string &, const std::string &, const ClanSearchState &, std::string &) {
    return true;
}
bool HasAccSaberEvidence(const std::vector<MapEntry>& scores, const std::string&) { return !scores.empty(); }
bool FetchAccSaberRecommendations(const std::string&, const std::string&, ListKind, int, double,
    ScoreHistory& history, std::vector<MapEntry>& entries, std::string& error) {
    ++fetches;
    if (failFetch) { error = "AccSaber unavailable"; return false; }
    history.loaded = true; history.scores.resize(1); entries.resize(1); return true;
}
bool WriteAccSaberPlaylist(ListKind kind, const std::vector<MapEntry>& entries, std::string& error, const std::string&) {
    return WritePlaylist("AccSaber", kind, entries, error);
}
bool FetchSyncPlaylist(const std::vector<SyncAccount>& accounts, int required, std::vector<MapEntry>& entries,
                       std::string& error) {
    const auto& bl=accounts[0].playerId; const auto& ss=accounts[1].playerId;
    auto* blHistory=accounts[0].history; auto* ssHistory=accounts[1].history;
    CHECK(required == 2 || required == 3);
    ++fetches;
    if (throwSync) throw std::runtime_error("AccSaber simulated exception");
    if (failSyncOnce) {
        failSyncOnce = false;
        error = "AccSaber: request /maps?status=RANKED&size=100&page=0 failed: HTTP 503";
        return false;
    }
    CHECK(blHistory != nullptr && ssHistory != nullptr);
    if (failFetch || bl.empty() || ss.empty()) { error = "incomplete Sync input"; return false; }
    entries.resize(205);
    return true;
}
} // namespace rankedpractice
int main() {
    try {
        const auto diagnosticDirectory = Utf8Path("tests/pcvr-sandbox/refresh-errors-\xC3\x85");
        RuntimePaths().icons = diagnosticDirectory;
        const auto diagnosticFile = diagnosticDirectory / "last-refresh-error.log";
        std::error_code ec;
        std::filesystem::remove(diagnosticFile, ec);
        const auto diagnosticText = [&] {
            std::ifstream input(diagnosticFile, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        };
        RefreshSettings s;
        s.beatLeaderPlayerId = "123";
        s.scoreSaberPlayerId = "456";
        s.enableScoreSaber = false;
        s.enableClanPlaylist = false;
        std::string status;
        RefreshObserver observer;
        observer.status = [&](const std::string &text) { std::lock_guard lock(mutex); status = text; };
        Reset();
        auto result = RunRefresh(s, observer);
        CHECK(result.succeeded && result.filesChanged);
        CHECK(writes == std::vector<std::string>{"BeatLeader2"});
        CHECK(fetches == 1);
        CHECK(!capturedBeatLeaderModifiers);
        CHECK(!std::filesystem::exists(diagnosticFile)); // Successful refreshes do not manufacture error reports.
        Reset();
        s.separateNewAndRepeatTracks = true;
        s.beatLeaderModifiers = true;
        result = RunRefresh(s, observer);
        CHECK(result.succeeded);
        CHECK((writes == std::vector<std::string>{"BeatLeader0", "BeatLeader1"}));
        CHECK(fetches == 2);
        CHECK(capturedBeatLeaderModifiers);
        Reset();
        failFetch = true;
        result = RunRefresh(s, observer);
        CHECK(!result.succeeded);
        CHECK(writes.empty());
        CHECK(prunes.size() == 2 && prunes[1] == "ScoreSaber");
        Reset();
        failWrite = true;
        result = RunRefresh(s, observer);
        CHECK(!result.succeeded);
        CHECK(writes.empty());
        Reset();
        s.separateNewAndRepeatTracks = false;
        result = RunRefresh(s, observer);
        CHECK(result.succeeded);
        CHECK(writes == std::vector<std::string>{"BeatLeader2"});
        Reset();
        s.enableBeatLeader = false;
        s.enableClanPlaylist = true;
        s.allClans = true;
        result = RunRefresh(s, observer);
        CHECK(result.succeeded);
        CHECK(writes.size() == 2);
        CHECK(prunes.back() == "clans");
        CHECK(capturedClanModifiers);
        Reset();
        failClan = true;
        result = RunRefresh(s, observer);
        CHECK(!result.succeeded);
        CHECK(writes == std::vector<std::string>{"A"});
        CHECK(prunes.size() == 3);
        Reset();
        failMember = true;
        result = RunRefresh(s, observer);
        CHECK(!result.succeeded);
        CHECK(writes.empty());
        CHECK(prunes.size() == 3);
        Reset();
        emptyMembership = true;
        result = RunRefresh(s, observer);
        CHECK(result.succeeded);
        CHECK(writes.empty());
        CHECK(prunes.back() == "clans");
        Reset();
        s.enableClanPlaylist = false;
        result = RunRefresh(s, observer);
        CHECK(!result.succeeded);
        CHECK(fetches == 0);
        CHECK(status.find("Enable at least") != std::string::npos);
        Reset();
        s.enableBeatLeader = true;
        s.enableScoreSaber = true;
        s.separateNewAndRepeatTracks = true;
        result = RunRefresh(s, {});
        CHECK(result.succeeded);
        CHECK(writes.size() == 4);
        CHECK(fetches == 4);
        Reset();
        s.enableBeatLeader = s.enableScoreSaber = s.enableClanPlaylist = false;
        s.enableSyncPlaylist = true; s.limit = 1; s.separateNewAndRepeatTracks = false; s.minClearRate = .95;
        size_t syncSongs = 0;
        observer.playlist = [&](const std::string& service, ListKind kind, const std::vector<MapEntry>& entries) {
            CHECK(service == "Sync" && kind == ListKind::Sync); syncSongs = entries.size();
        };
        result = RunRefresh(s, observer);
        CHECK(result.succeeded && result.filesChanged && syncSongs == 205 && fetches == 1);
        CHECK(writes == std::vector<std::string>{"Sync3"});
        Reset(); failFetch = true;
        std::string syncError;
        observer.syncError = [&](const std::string& error) { syncError = error; };
        result = RunRefresh(s, observer);
        CHECK(!result.succeeded && writes.empty() && !syncError.empty());
        Reset(); failWrite = true;
        result = RunRefresh(s, observer);
        CHECK(!result.succeeded && writes.empty() && syncError == "disk full");
        Reset(); s.scoreSaberPlayerId.clear();
        result = RunRefresh(s, observer);
        CHECK(!result.succeeded && writes.empty());
        Reset(); observer = {}; s.enableSyncPlaylist = false; s.enableAccSaber = true; s.accSaberPlayerId = "789";
        result = RunRefresh(s, observer);
        CHECK(result.succeeded && writes == std::vector<std::string>{"AccSaber2"});
        Reset(); s.separateNewAndRepeatTracks = true;
        result = RunRefresh(s, observer);
        CHECK(result.succeeded && writes.size() == 2 && fetches == 2);
        Reset(); failFetch = true;
        result = RunRefresh(s, observer);
        CHECK(!result.succeeded && writes.empty());
        Reset(); failWrite = true;
        result = RunRefresh(s, observer);
        CHECK(!result.succeeded && writes.empty());
        // Selecting AccSaber only makes it a Sync participant; its personal
        // playlist toggle remains off. A one-off failure must remain diagnosable.
        RefreshSettings syncOnly;
        syncOnly.beatLeaderPlayerId = "123"; syncOnly.scoreSaberPlayerId = "456"; syncOnly.accSaberPlayerId = "789";
        syncOnly.enableBeatLeader = syncOnly.enableScoreSaber = syncOnly.enableAccSaber = syncOnly.enableClanPlaylist = false;
        syncOnly.enableSyncPlaylist = true;
        observer = {}; observer.status = [&](const std::string& text) { std::lock_guard lock(mutex); status = text; };
        Reset(); failSyncOnce = true;
        result = RunRefresh(syncOnly, observer);
        CHECK(!result.succeeded && writes.empty());
        CHECK(status.find("Refresh finished with errors:") != std::string::npos);
        CHECK(status.find("Sync failed") != std::string::npos && status.find("AccSaber") != std::string::npos);
        CHECK(status.find("HTTP 503") != std::string::npos && status.find("last-refresh-error.log") != std::string::npos);
        const auto savedFailure = diagnosticText();
        CHECK(savedFailure.find("HTTP 503") != std::string::npos && savedFailure.find("page=0") != std::string::npos);
        CHECK(savedFailure.find("AccSaber=0") != std::string::npos && savedFailure.find("Sync=1") != std::string::npos);
        CHECK(savedFailure.find("AccSaber=789") != std::string::npos);
        CHECK(!std::filesystem::exists(diagnosticDirectory / "last-refresh-error.log.tmp"));
        Reset();
        result = RunRefresh(syncOnly, observer);
        CHECK(result.succeeded && writes == std::vector<std::string>{"Sync3"});
        CHECK(status.find("Playlists updated") != std::string::npos && status.find("HTTP 503") == std::string::npos);
        CHECK(diagnosticText() == savedFailure); // A successful retry must not erase the original evidence.
        Reset(); throwSync = true;
        result = RunRefresh(syncOnly, observer);
        CHECK(!result.succeeded && writes.empty());
        CHECK(status.find("AccSaber simulated exception") != std::string::npos);
        CHECK(diagnosticText().find("Sync refresh stopped") != std::string::npos);
        Reset(); failFetch = true;
        auto parallel = syncOnly; parallel.enableBeatLeader = parallel.enableScoreSaber = true;
        parallel.enableSyncPlaylist = false; parallel.separateNewAndRepeatTracks = true;
        result = RunRefresh(parallel, observer);
        CHECK(!result.succeeded && writes.empty());
        CHECK(status.find("BeatLeader") != std::string::npos && status.find("ScoreSaber") != std::string::npos);
        CHECK(diagnosticText().find("BeatLeader") != std::string::npos && diagnosticText().find("ScoreSaber") != std::string::npos);
        const auto preservedFailure = diagnosticText();
        const auto blockedDirectory = diagnosticDirectory / "blocked-path";
        { std::ofstream blocked(blockedDirectory); blocked << "not a directory"; }
        RuntimePaths().icons = blockedDirectory;
        Reset(); failSyncOnce = true;
        result = RunRefresh(syncOnly, observer);
        CHECK(!result.succeeded && status.find("HTTP 503") != std::string::npos);
        CHECK(status.find("Could not save the error report") != std::string::npos);
        CHECK(diagnosticText() == preservedFailure);
        RuntimePaths().icons = diagnosticDirectory;
        std::cout << checks << " refresh orchestration checks passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
