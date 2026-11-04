#include "bridge.h"
#include "platform.hpp"
#include "refresh_engine.hpp"
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <stdexcept>
#include <thread>
using namespace rankedpractice;
namespace {
int checks{};
std::mutex barrierMutex;
std::condition_variable barrier;
bool entered{}, releaseWorker{};
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(x))                                                                                                      \
            throw std::runtime_error(#x);                                                                              \
    } while (false)
rapidjson::Document Call(const std::string &text, bool expected = true) {
    char *output = nullptr;
    size_t length{};
    const auto result = rt_call(text.data(), text.size(), &output, &length);
    CHECK(output != nullptr);
    CHECK((result != 0) == expected);
    rapidjson::Document document;
    document.Parse(output, length);
    rt_free(output);
    CHECK(!document.HasParseError() && document.IsObject());
    CHECK(document["ok"].GetBool() == expected);
    return document;
}
} // namespace
namespace rankedpractice {
// Fixture-backed provider boundary. The actual bridge, models, chart renderer,
// storage, serializers, and C allocation/free functions are production code.
RefreshResult RunRefresh(const RefreshSettings &settings, const RefreshObserver &observer) {
    {
        std::unique_lock lock(barrierMutex);
        entered = true;
        barrier.notify_all();
        barrier.wait(lock, [] { return releaseWorker; });
    }
    std::vector<MapEntry> scores;
    for (int i = 0; i < 24; i++) {
        constexpr char hex[] = "0123456789abcdef";
        MapEntry e;
        e.hash = std::string(38, 'a') + hex[i / 16] + hex[i % 16];
        e.songName = "\xc3\x85 fixture";
        e.accuracy = .99 - i * .003;
        e.pp = 100 + i;
        e.stars = 3 + i * .2;
        e.difficulties = {{"Standard", "Expert", e.stars, 7, 8, 5}};
        e.predictedAccuracy = .95;
        e.predictedPp = 123.45;
        scores.push_back(e);
    }
    observer.ability("BeatLeader", scores, "");
    observer.playlist("BeatLeader", ListKind::Combined, {scores.front()});
    if (settings.enableSyncPlaylist) {
        scores.front().syncTargets = {{"OneSaber", "Expert", "ScoreSaber"}, {"Standard", "Hard", "BeatLeader"}};
        observer.playlist("Sync", ListKind::Sync, {scores.front()});
    }
    observer.status("Fixture refresh completed.");
    return {true, true};
}
} // namespace rankedpractice
int main() {
    const auto root =
        std::filesystem::absolute(Utf8Path("tests/pcvr-sandbox/bridge-\xc3\x85-\xe6\xb5\x8b\xe8\xaf\x95"));
    try {
        CHECK(rt_abi_version() == 1);
        rt_free(nullptr);
        Call("not json", false);
        Call("[]", false);
        Call("{\"command\":\"snapshot\"}", false);
        CHECK(!std::filesystem::exists(root));
        std::filesystem::create_directories(root);
        std::filesystem::copy_file("cacert.pem", root / "cacert.pem");
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
        w.StartObject();
        w.Key("command");
        w.String("initialize");
        w.Key("dataDirectory");
        w.String(PathUtf8(root).c_str());
        w.Key("playlistDirectory");
        w.String(PathUtf8(root / "Playlists").c_str());
        w.EndObject();
        Call(buffer.GetString());
        Call(buffer.GetString(), false);
        Call("{\"command\":\"unknown\"}", false);
        Call("{\"command\":\"refresh\",\"beatLeaderPlayerId\":123}", false);
        const std::string refresh =
            R"({"command":"refresh","beatLeaderPlayerId":"76561198000000001","enableScoreSaber":false,"enableClanPlaylist":false,"enableSyncPlaylist":true,"simpleMode":true})";
        std::string workerError;
        std::thread worker([&] {
            try {
                Call(refresh);
            } catch (const std::exception &e) {
                workerError = e.what();
            }
        });
        {
            std::unique_lock lock(barrierMutex);
            barrier.wait(lock, [] { return entered; });
        }
        const auto overlap = Call(refresh, false);
        CHECK(std::string(overlap["error"].GetString()).find("already running") != std::string::npos);
        auto snapshot = Call(R"({"command":"snapshot"})");
        CHECK(snapshot["running"].GetBool());
        {
            std::lock_guard lock(barrierMutex);
            releaseWorker = true;
        }
        barrier.notify_all();
        worker.join();
        CHECK(workerError.empty());
        snapshot = Call(R"({"command":"snapshot"})");
        CHECK(!snapshot["running"].GetBool());
        CHECK(snapshot["succeeded"].GetBool());
        CHECK(snapshot["playlists"].Size() == 2);
        const auto& sync = snapshot["playlists"][1];
        CHECK(sync["kind"].GetDouble() == 3 && sync["ready"].GetBool());
        CHECK(std::string(sync["fileName"].GetString()) == "RankedPractice_Sync.bplist");
        const auto& targets = sync["entries"][0]["syncTargets"];
        CHECK(targets.Size() == 2);
        CHECK(std::string(targets[0]["characteristic"].GetString()) == "OneSaber");
        CHECK(std::string(targets[1]["missingService"].GetString()) == "BeatLeader");
        CHECK(snapshot["playlists"][0]["entries"][0]["predictedPp"].GetDouble() == 123.45);
        CHECK(std::string(snapshot["playlists"][0]["entries"][0]["songName"].GetString()) == "\xc3\x85 fixture");
        CHECK(snapshot["abilities"][0]["scoreCount"].GetDouble() == 24);
        void *pixels = nullptr;
        size_t size{};
        int width{}, height{};
        CHECK(rt_chart(0, &pixels, &size, &width, &height) == 1);
        CHECK(pixels != nullptr && size == size_t(width * height * 4));
        CHECK(width == 1024 && height == 576);
        rt_free(pixels);
        CHECK(rt_chart(-1, &pixels, &size, &width, &height) == 0 && pixels == nullptr && size == 0);
        CHECK(rt_chart(1, &pixels, &size, &width, &height) == 0 && pixels == nullptr);
        Call(R"({"command":"attempt","hash":"bad"})", false);
        Call(R"({"command":"accounts","service":"unknown","input":"123"})", false);
        Call(R"({"command":"shutdown"})");
        std::filesystem::remove(root / "cacert.pem");
        std::filesystem::remove(root / "Playlists");
        std::filesystem::remove(root / "attempts");
        std::filesystem::remove(root / "clan-search");
        std::filesystem::remove(root);
        std::cout << checks
                  << " native ABI checks passed (UTF-8, errors, overlap, snapshots, chart transfer, ownership).\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
