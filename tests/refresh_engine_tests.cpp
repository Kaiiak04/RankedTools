// Exercise production orchestration with injected provider/storage boundaries.
#include "refresh_engine.hpp"
#include <atomic>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>
using namespace rankedpractice;
namespace {
std::mutex mutex;
std::vector<std::string> writes, prunes;
bool failFetch{}, failWrite{}, failClan{}, failMember{}, emptyMembership{};
int checks{}, fetches{};
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
    fetches = 0;
}
} // namespace
namespace rankedpractice {
HttpStatistics GetHttpStatistics() { return {}; }
std::string PlaylistFileName(const std::string &service, ListKind kind) {
    return service + std::to_string(static_cast<int>(kind));
}
std::string PlaylistTitle(const std::string &service, ListKind kind) { return PlaylistFileName(service, kind); }
std::string ClanPlaylistFileName(const std::string &clan, bool all) { return clan + (all ? "_all" : "_main"); }
bool FetchRecommendations(const std::string &, const std::string &, ListKind kind, int, std::vector<MapEntry> &entries,
                          std::string &error, double, StarRange *, ScoreHistory *history) {
    std::lock_guard lock(mutex);
    ++fetches;
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
                        std::string &error, double, ScoreHistory *, bool legacy) {
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
bool FetchSyncPlaylist(const std::string& bl, const std::string& ss, std::vector<MapEntry>& entries,
                       std::string& error, ScoreHistory* blHistory, ScoreHistory* ssHistory) {
    ++fetches;
    CHECK(blHistory != nullptr && ssHistory != nullptr);
    if (failFetch || bl.empty() || ss.empty()) { error = "incomplete Sync input"; return false; }
    entries.resize(205);
    return true;
}
} // namespace rankedpractice
int main() {
    try {
        RefreshSettings s;
        s.beatLeaderPlayerId = "123";
        s.scoreSaberPlayerId = "456";
        s.enableScoreSaber = false;
        s.enableClanPlaylist = false;
        std::string status;
        RefreshObserver observer;
        observer.status = [&](const std::string &text) { status = text; };
        Reset();
        auto result = RunRefresh(s, observer);
        CHECK(result.succeeded && result.filesChanged);
        CHECK(writes.size() == 2);
        CHECK(fetches == 2);
        Reset();
        failFetch = true;
        result = RunRefresh(s, observer);
        CHECK(!result.succeeded);
        CHECK(writes.empty());
        CHECK(prunes.size() == 1 && prunes[0] == "ScoreSaber");
        Reset();
        failWrite = true;
        result = RunRefresh(s, observer);
        CHECK(!result.succeeded);
        CHECK(writes.empty());
        Reset();
        s.simpleMode = true;
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
        Reset();
        failClan = true;
        result = RunRefresh(s, observer);
        CHECK(!result.succeeded);
        CHECK(writes == std::vector<std::string>{"A"});
        CHECK(prunes.size() == 2);
        Reset();
        failMember = true;
        result = RunRefresh(s, observer);
        CHECK(!result.succeeded);
        CHECK(writes.empty());
        CHECK(prunes.size() == 2);
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
        s.simpleMode = false;
        result = RunRefresh(s, {});
        CHECK(result.succeeded);
        CHECK(writes.size() == 4);
        CHECK(fetches == 4);
        Reset();
        s.enableBeatLeader = s.enableScoreSaber = s.enableClanPlaylist = false;
        s.enableSyncPlaylist = true; s.limit = 1; s.simpleMode = true; s.minClearRate = .95;
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
        std::cout << checks << " refresh orchestration checks passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
