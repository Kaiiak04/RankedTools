#include "bridge.h"
#include "ability_chart.hpp"
#include "account.hpp"
#include "platform.hpp"
#include "playlist_image.hpp"
#include "refresh_engine.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <stdexcept>

namespace {
using namespace rankedpractice;
using Json = rapidjson::Value;
using Writer = rapidjson::Writer<rapidjson::StringBuffer>;
struct List {
    std::string service, title, fileName, note, error;
    int kind{};
    bool ready{}, clan{}, legacy{}, hypothetical{};
    std::vector<MapEntry> entries;
};
struct Ability {
    bool ready{};
    std::string error;
    AbilityChart chart;
};
std::mutex stateMutex, initializationMutex;
std::atomic<bool> initialized{false}, running{false};
std::string status{"Open RankedTools to refresh recommendations."};
std::vector<List> lists;
std::array<Ability, 2> abilities;
std::deque<std::string> logs;
std::uint64_t logSequence{};
bool succeeded{}, filesChanged{};
RefreshSettings snapshotSettings;

std::string String(const Json &j, const char *key, const std::string &fallback = {}) {
    if (!j.IsObject() || !j.HasMember(key))
        return fallback;
    const auto &value = j[key];
    if (!value.IsString())
        throw std::invalid_argument(std::string(key) + " must be a string.");
    std::string text(value.GetString(), value.GetStringLength());
    if (text.find('\0') != std::string::npos)
        throw std::invalid_argument("Embedded NUL in a string.");
    return text;
}
bool Bool(const Json &j, const char *key, bool fallback) {
    if (!j.HasMember(key))
        return fallback;
    if (!j[key].IsBool())
        throw std::invalid_argument(std::string(key) + " must be a boolean.");
    return j[key].GetBool();
}
double Number(const Json &j, const char *key, double fallback = 0) {
    if (!j.HasMember(key))
        return fallback;
    if (!j[key].IsNumber() || !std::isfinite(j[key].GetDouble()))
        throw std::invalid_argument(std::string(key) + " must be a finite number.");
    return j[key].GetDouble();
}
void Text(Writer &w, const char *key, const std::string &text) {
    w.Key(key);
    w.String(text.data(), static_cast<rapidjson::SizeType>(text.size()));
}
void Numeric(Writer &w, const char *key, double value) {
    w.Key(key);
    if (std::isfinite(value))
        w.Double(value);
    else
        w.Null();
}
void Flag(Writer &w, const char *key, bool value) {
    w.Key(key);
    w.Bool(value);
}
void Log(const std::string &text) {
    std::lock_guard lock(stateMutex);
    logs.push_back(text);
    ++logSequence;
    if (logs.size() > 100)
        logs.pop_front();
}
void EntryJson(Writer &w, const MapEntry &e) {
    w.StartObject();
    Text(w, "hash", e.hash);
    Text(w, "songName", e.songName);
    Text(w, "songAuthorName", e.songAuthorName);
    Numeric(w, "predictedAccuracy", e.predictedAccuracy);
    Numeric(w, "predictedPp", e.predictedPp);
    Numeric(w, "currentPp", e.currentPp);
    Numeric(w, "weightedPpGain", e.weightedPpGain);
    Numeric(w, "clearChance", e.clearChance);
    Numeric(w, "confidence", e.confidence);
    Numeric(w, "nearbyClears", e.nearbyClears);
    Numeric(w, "nearbyFailures", e.nearbyFailures);
    Numeric(w, "nearbyNoFail", e.nearbyNoFail);
    Numeric(w, "priority", e.priority);
    Flag(w, "isImprovement", e.isImprovement);
    w.Key("difficulties");
    w.StartArray();
    for (const auto &d : e.difficulties) {
        w.StartObject();
        Text(w, "name", d.name);
        Text(w, "characteristic", d.characteristic);
        Numeric(w, "stars", d.stars);
        w.EndObject();
    }
    w.EndArray();
    w.Key("syncTargets");
    w.StartArray();
    for (const auto &target : e.syncTargets) {
        w.StartObject();
        Text(w, "characteristic", target.characteristic);
        Text(w, "difficulty", target.difficulty);
        Text(w, "missingService", target.missingService);
        w.EndObject();
    }
    w.EndArray();
    if (e.clanImpact) {
        const auto &c = *e.clanImpact;
        w.Key("clanImpact");
        w.StartObject();
        Numeric(w, "gap", c.gap);
        Numeric(w, "gain", c.gain);
        Numeric(w, "gapClosure", c.gapClosure);
        Flag(w, "captures", c.captures);
        Flag(w, "hypothetical", c.hypothetical);
        if (c.requiredPp)
            Numeric(w, "requiredPp", *c.requiredPp);
        if (c.requiredAccuracy)
            Numeric(w, "requiredAccuracy", *c.requiredAccuracy);
        w.EndObject();
    }
    w.EndObject();
}
void Snapshot(Writer &w) {
    std::lock_guard lock(stateMutex);
    Flag(w, "running", running.load());
    Flag(w, "succeeded", succeeded);
    Flag(w, "filesChanged", filesChanged);
    Text(w, "status", status);
    Numeric(w, "minClearRate", snapshotSettings.minClearRate * 100);
    Flag(w, "enableBeatLeader", snapshotSettings.enableBeatLeader);
    Flag(w, "enableScoreSaber", snapshotSettings.enableScoreSaber);
    w.Key("logSequence");
    w.Uint64(logSequence);
    w.Key("logs");
    w.StartArray();
    for (const auto &log : logs)
        w.String(log.c_str());
    w.EndArray();
    w.Key("playlists");
    w.StartArray();
    for (const auto &list : lists) {
        w.StartObject();
        Text(w, "service", list.service);
        Text(w, "title", list.title);
        Text(w, "fileName", list.fileName);
        Text(w, "note", list.note);
        Text(w, "error", list.error);
        Numeric(w, "kind", list.kind);
        Flag(w, "ready", list.ready);
        Flag(w, "clan", list.clan);
        Flag(w, "legacy", list.legacy);
        Flag(w, "hypothetical", list.hypothetical);
        w.Key("entries");
        w.StartArray();
        for (const auto &entry : list.entries)
            EntryJson(w, entry);
        w.EndArray();
        w.EndObject();
    }
    w.EndArray();
    w.Key("abilities");
    w.StartArray();
    for (int i = 0; i < 2; ++i) {
        const auto &a = abilities[i];
        w.StartObject();
        Text(w, "service", i == 0 ? "BeatLeader" : "ScoreSaber");
        Flag(w, "ready", a.ready);
        Text(w, "error", a.error);
        Numeric(w, "scoreCount", a.chart.scores.size());
        Numeric(w, "minStars", a.chart.minObservedStars);
        Numeric(w, "maxStars", a.chart.maxObservedStars);
        w.EndObject();
    }
    w.EndArray();
}
RefreshSettings Settings(const Json &j) {
    RefreshSettings s;
    s.beatLeaderPlayerId = String(j, "beatLeaderPlayerId");
    s.scoreSaberPlayerId = String(j, "scoreSaberPlayerId");
    for (const auto *id : {&s.beatLeaderPlayerId, &s.scoreSaberPlayerId})
        if (!id->empty() && !IsCanonicalAccountId(*id))
            throw std::invalid_argument("Select a valid numeric player ID.");
    s.limit = static_cast<int>(std::clamp(Number(j, "limit", 16), 1.0, 100.0));
    s.minClearRate =
        NormalizeMinClearRatePercent(static_cast<int>(std::clamp(Number(j, "minClearRate", 50), 0.0, 95.0))) / 100.0;
    s.simpleMode = Bool(j, "simpleMode", false);
    s.importAttempts = Bool(j, "importAttempts", true);
    s.enableBeatLeader = Bool(j, "enableBeatLeader", true);
    s.enableScoreSaber = Bool(j, "enableScoreSaber", true);
    s.enableClanPlaylist = Bool(j, "enableClanPlaylist", true);
    s.enableSyncPlaylist = Bool(j, "enableSyncPlaylist", false);
    s.legacyClanPlaylists = Bool(j, "legacyClanPlaylists", false);
    s.allClans = Bool(j, "allClans", false);
    return s;
}
void Refresh(const Json &j) {
    const auto settings = Settings(j);
    bool expected = false;
    if (!running.compare_exchange_strong(expected, true))
        throw std::runtime_error("A recommendation refresh is already running.");
    struct Guard {
        ~Guard() { running = false; }
    } guard;
    {
        std::lock_guard lock(stateMutex);
        lists.clear();
        abilities = {};
        succeeded = false;
        filesChanged = false;
        snapshotSettings = settings;
        status = "Fetching leaderboard data...";
        for (const auto *service : {"BeatLeader", "ScoreSaber"}) {
            if (!(std::string(service) == "BeatLeader" ? settings.enableBeatLeader : settings.enableScoreSaber))
                continue;
            for (const auto kind : settings.simpleMode ? std::vector{ListKind::Combined}
                                                       : std::vector{ListKind::NotPlayed, ListKind::ToImprove}) {
                List list;
                list.service = service;
                list.kind = static_cast<int>(kind);
                list.title = PlaylistTitle(service, kind);
                list.fileName = PlaylistFileName(service, kind);
                lists.push_back(std::move(list));
            }
        }
    }
    if (settings.enableSyncPlaylist) {
        std::lock_guard lock(stateMutex);
        List list;
        list.service = "Sync";
        list.kind = static_cast<int>(ListKind::Sync);
        list.title = "Sync";
        list.fileName = PlaylistFileName("Sync", ListKind::Sync);
        lists.push_back(std::move(list));
    }
    RefreshObserver observer;
    observer.syncError = [](const std::string &error) {
        std::lock_guard lock(stateMutex);
        for (auto &list : lists) if (list.kind == static_cast<int>(ListKind::Sync)) {
            list.ready = true;
            list.error = error;
        }
    };
    observer.status = [](const std::string &text) {
        std::lock_guard lock(stateMutex);
        status = text;
    };
    observer.log = Log;
    observer.playlist = [](const std::string &service, ListKind kind, const std::vector<MapEntry> &entries) {
        std::lock_guard lock(stateMutex);
        for (auto &list : lists)
            if (!list.clan && list.service == service && list.kind == static_cast<int>(kind)) {
                list.ready = true;
                list.entries = entries;
                filesChanged = true;
            }
    };
    observer.ability = [](const std::string &service, const std::vector<MapEntry> &scores, const std::string &error) {
        auto chart = BuildAbilityChart(scores);
        std::lock_guard lock(stateMutex);
        abilities[service == "ScoreSaber" ? 1 : 0] = {true, error, std::move(chart)};
    };
    observer.clans = [all = settings.allClans](const std::vector<ClanPlaylistRecommendations> &clans) {
        std::lock_guard lock(stateMutex);
        lists.erase(std::remove_if(lists.begin(), lists.end(), [](const List &list) { return list.clan; }),
                    lists.end());
        for (const auto &clan : clans) {
            List list;
            list.service = "BeatLeader";
            list.clan = true;
            list.ready = true;
            list.legacy = clan.legacy;
            list.hypothetical = clan.hypothetical;
            list.entries = clan.entries;
            list.error = clan.error;
            list.title = ClanPlaylistTitle(clan.clanTag + (clan.legacy ? "; legacy" : ""));
            list.fileName = ClanPlaylistFileName(clan.clanTag, all);
            if (clan.hypothetical)
                list.note = "What-if primary clan: requires changing your primary clan.\n";
            if (clan.legacy) {
                list.note += "Legacy clan-gap order. ";
                list.note +=
                    std::isfinite(clan.legacyMaxStars)
                        ? "Automatic maximum: " + std::to_string(clan.legacyMaxStars) + " stars; no lower star limit.\n"
                        : "Maximum stars disabled at 0% Min Clear Rate.\n";
                list.note += "Keeps unplayed charts and predicted personal improvements.\n";
                if (clan.hypothetical)
                    list.note += "Order uses current clan totals before any primary-clan transfer.\n";
            } else
                list.note += "Estimated captures first, then useful progress.\nVerified " +
                             std::to_string(clan.checkedCharts - clan.failedCharts) + " of " +
                             std::to_string(clan.eligibleCharts) +
                             " eligible charts; targeted search may miss opportunities.\nRechecked " +
                             std::to_string(clan.recheckedCharts) + " previous picks; " +
                             std::to_string(clan.learnedCharts) + " learned screens; " +
                             std::to_string(clan.failedCharts) + " verification failures.\n";
            list.note += clan.warning;
            if (clan.error.empty())
                filesChanged = true;
            lists.push_back(std::move(list));
        }
    };
    try {
        const auto result = RunRefresh(settings, observer);
        std::lock_guard lock(stateMutex);
        succeeded = result.succeeded;
        filesChanged = filesChanged || result.filesChanged;
    } catch (...) {
        std::lock_guard lock(stateMutex);
        status = "Refresh stopped unexpectedly; previous output is preserved where generation failed.";
        throw;
    }
}
void Initialize(const Json &j) {
    std::lock_guard lock(initializationMutex);
    if (initialized)
        throw std::runtime_error("The engine has already been initialized.");
    PlatformPaths paths;
    const auto root = Utf8Path(String(j, "dataDirectory"));
    paths.playlists = Utf8Path(String(j, "playlistDirectory"));
    if (!root.is_absolute() || !paths.playlists.is_absolute())
        throw std::invalid_argument("Storage paths must be absolute.");
    paths.attempts = root / "attempts";
    paths.clanSearch = root / "clan-search";
    paths.icons = root / "assets";
    paths.caBundle = root / "cacert.pem";
    paths.userAgent = "RankedTools/0.1.26-pcvr.1 (Beat Saber PCVR mod)";
    if (!std::filesystem::is_regular_file(paths.caBundle))
        throw std::runtime_error("RankedTools' CA bundle is missing. Extract the complete preview ZIP.");
    if (std::filesystem::file_size(paths.caBundle) > 2 * 1024 * 1024)
        throw std::runtime_error("The CA bundle is oversized.");
    std::ifstream certificates(paths.caBundle, std::ios::binary);
    paths.caCertificates.assign(std::istreambuf_iterator<char>(certificates), {});
    if (paths.caCertificates.empty() || !certificates)
        throw std::runtime_error("Could not read the CA bundle.");
    std::filesystem::create_directories(paths.attempts);
    std::filesystem::create_directories(paths.clanSearch);
    std::filesystem::create_directories(paths.playlists);
    RuntimePaths() = std::move(paths);
    initialized = true;
}
void SubmitAttempt(const Json &j) {
    Attempt a;
    a.source = "local";
    a.id = String(j, "id");
    a.hash = String(j, "hash");
    a.characteristic = String(j, "characteristic", "Standard");
    a.difficulty = String(j, "difficulty");
    a.modifiers = String(j, "modifiers");
    a.timestamp = Number(j, "timestamp");
    a.endTime = Number(j, "endTime");
    a.normal = Bool(j, "normal", true);
    a.practice = Bool(j, "practice", false);
    const auto outcome = Number(j, "outcome");
    if (outcome < 0 || outcome > 5 || outcome != std::floor(outcome))
        throw std::invalid_argument("Invalid attempt outcome.");
    a.outcome = static_cast<AttemptOutcome>(static_cast<int>(outcome));
    static const std::vector<std::string> difficulties{"Easy", "Normal", "Hard", "Expert", "ExpertPlus"};
    if (a.id.empty() || a.id.size() > 128 || a.hash.size() != 40 ||
        !std::all_of(a.hash.begin(), a.hash.end(), [](unsigned char c) { return std::isxdigit(c) != 0; }) ||
        a.timestamp <= 0 || a.endTime < 0 || a.characteristic.empty() ||
        std::find(difficulties.begin(), difficulties.end(), a.difficulty) == difficulties.end())
        throw std::invalid_argument("Invalid local attempt.");
    for (const auto &pair : {std::pair{"beatleader", String(j, "beatLeaderPlayerId")},
                             std::pair{"scoresaber", String(j, "scoreSaberPlayerId")}}) {
        if (pair.second.empty())
            continue;
        if (!IsCanonicalAccountId(pair.second))
            throw std::invalid_argument("Invalid attempt player ID.");
        std::vector<Attempt> merged;
        std::string error;
        if (!SaveAttemptHistory(pair.first, pair.second, {a}, merged, error))
            throw std::runtime_error(error);
    }
}
std::string Invoke(const Json &j) {
    const auto command = String(j, "command");
    rapidjson::StringBuffer buffer;
    Writer w(buffer);
    w.StartObject();
    Flag(w, "ok", true);
    if (command == "initialize")
        Initialize(j);
    else if (command == "shutdown")
        RuntimeCancelled() = true;
    else {
        if (!initialized)
            throw std::runtime_error("Initialize the engine before using it.");
        if (command == "snapshot")
            Snapshot(w);
        else if (command == "refresh") {
            Refresh(j);
            Snapshot(w);
        } else if (command == "attempt")
            SubmitAttempt(j);
        else if (command == "accounts") {
            const auto service = String(j, "service");
            if (service != "BeatLeader" && service != "ScoreSaber")
                throw std::invalid_argument("Unknown account service.");
            AccountPage page;
            std::string error;
            const int number = static_cast<int>(std::clamp(Number(j, "page", 1), 1.0, 100000.0));
            if (!FindAccounts(service == "BeatLeader" ? AccountService::BeatLeader : AccountService::ScoreSaber,
                              String(j, "input"), number, page, error))
                throw std::runtime_error(error);
            Numeric(w, "page", page.page);
            Flag(w, "hasNext", page.hasNext);
            w.Key("profiles");
            w.StartArray();
            for (const auto &profile : page.profiles) {
                w.StartObject();
                Text(w, "id", profile.id);
                Text(w, "name", profile.name);
                Text(w, "avatar", profile.avatar);
                Text(w, "country", profile.country);
                w.Key("rank");
                w.Int64(profile.rank);
                w.EndObject();
            }
            w.EndArray();
        } else if (command == "httpsProbe") {
            std::string error;
            const auto success = ProbeHttps(String(j, "url"), error);
            Flag(w, "success", success);
            Text(w, "error", error);
            Text(w, "tlsBackend", HttpsImplementation());
        } else if (command == "avatar") {
            std::vector<std::uint8_t> bytes;
            if (!FetchAccountAvatar(String(j, "url"), bytes))
                throw std::runtime_error("Avatar unavailable.");
            Text(w, "image", DownloadedPlaylistImage({reinterpret_cast<const char *>(bytes.data()), bytes.size()}));
        } else
            throw std::invalid_argument("Unknown engine command.");
    }
    w.EndObject();
    return {buffer.GetString(), buffer.GetSize()};
}
} // namespace
extern "C" int rt_abi_version(void) { return 1; }
extern "C" void rt_free(void *buffer) { std::free(buffer); }
extern "C" int rt_call(const char *request, size_t request_size, char **response, size_t *response_size) {
    if (!response || !response_size)
        return 0;
    *response = nullptr;
    *response_size = 0;
    int result = 1;
    std::string text;
    try {
        if (!request || request_size > 2 * 1024 * 1024)
            throw std::invalid_argument("Invalid engine request size.");
        rapidjson::Document j;
        j.Parse<rapidjson::kParseValidateEncodingFlag>(request, request_size);
        if (j.HasParseError() || !j.IsObject())
            throw std::invalid_argument("Expected a UTF-8 JSON object.");
        text = Invoke(j);
    } catch (const std::exception &e) {
        result = 0;
        rapidjson::StringBuffer buffer;
        Writer w(buffer);
        w.StartObject();
        Flag(w, "ok", false);
        Text(w, "error", e.what());
        w.EndObject();
        text.assign(buffer.GetString(), buffer.GetSize());
    } catch (...) {
        result = 0;
        text = "{\"ok\":false,\"error\":\"Unknown native engine error.\"}";
    }
    auto *copy = static_cast<char *>(std::malloc(text.size() + 1));
    if (!copy)
        return 0;
    std::memcpy(copy, text.c_str(), text.size() + 1);
    *response = copy;
    *response_size = text.size();
    return result;
}
extern "C" int rt_chart(int service, void **pixels, size_t *size, int *width, int *height) {
    if (!pixels || !size || !width || !height)
        return 0;
    *pixels = nullptr;
    *size = 0;
    *width = 0;
    *height = 0;
    try {
        if (service < 0 || service > 1)
            return 0;
        AbilityChart chart;
        {
            std::lock_guard lock(stateMutex);
            const auto &a = abilities[service];
            if (!a.ready || !a.error.empty() || a.chart.scores.empty())
                return 0;
            chart = a.chart;
        }
        const auto bitmap = RenderAbilityChart(chart);
        auto *memory = std::malloc(bitmap.pixels.size());
        if (!memory)
            return 0;
        std::memcpy(memory, bitmap.pixels.data(), bitmap.pixels.size());
        *pixels = memory;
        *size = bitmap.pixels.size();
        *width = bitmap.width;
        *height = bitmap.height;
        return 1;
    } catch (...) {
        return 0;
    }
}
