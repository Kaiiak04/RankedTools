#include "attempt_history.hpp"
#include "platform.hpp"
#include "playlist_writer.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace rankedpractice;
namespace {
int checks{};
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(x))                                                                                                      \
            throw std::runtime_error(#x);                                                                              \
    } while (false)
std::string Read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
} // namespace
int main() {
    const auto root = Utf8Path("tests/pcvr-sandbox/native-\xc3\x85-\xe6\xb5\x8b\xe8\xaf\x95");
    try {
        CHECK(!std::filesystem::exists(root));
        RuntimePaths().playlists = root / "Playlists";
        RuntimePaths().attempts = root / "attempts";
        RuntimePaths().icons = root / "assets";
        std::filesystem::create_directories(root);
        CHECK(Utf8Path(PathUtf8(root)) == root);
        MapEntry entry;
        entry.hash = std::string(40, 'a');
        entry.songName = "Unicode \xc3\x85";
        entry.difficulties = {{"Standard", "Expert", 7}};
        std::string error;
        CHECK(WritePlaylist("BeatLeader", ListKind::Combined, {entry}, error));
        const auto playlist = RuntimePaths().playlists / PlaylistFileName("BeatLeader", ListKind::Combined);
        const auto first = Read(playlist);
        CHECK(first.find(entry.songName) != std::string::npos);
        entry.songName = "Replacement";
        CHECK(WritePlaylist("BeatLeader", ListKind::Combined, {entry}, error));
        const auto second = Read(playlist);
        CHECK(second != first && second.find("Replacement") != std::string::npos);
#ifdef _WIN32
        const auto handle = CreateFileW(playlist.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(handle != INVALID_HANDLE_VALUE);
        entry.songName = "Must not replace locked file";
        const auto written = WritePlaylist("BeatLeader", ListKind::Combined, {entry}, error);
        CloseHandle(handle);
        CHECK(!written && !error.empty());
        CHECK(Read(playlist) == second);
#endif
        Attempt a;
        a.id = "one";
        a.source = "local";
        a.hash = entry.hash;
        a.difficulty = "Expert";
        a.timestamp = 1700000000;
        a.outcome = AttemptOutcome::Clear;
        std::vector<Attempt> history;
        CHECK(SaveAttemptHistory("beatleader", "123", {a}, history, error));
        a.id = "two";
        a.timestamp += 300;
        a.outcome = AttemptOutcome::Fail;
        CHECK(SaveAttemptHistory("beatleader", "123", {a}, history, error));
        CHECK(history.size() == 2);
        CHECK(ReadAttemptHistory("beatleader", "123", history, error));
        CHECK(history.size() == 2);
        const auto attempts = RuntimePaths().attempts / "beatleader_123.json";
        {
            std::ofstream bad(attempts, std::ios::binary | std::ios::trunc);
            bad << "damaged";
        }
        CHECK(!SaveAttemptHistory("beatleader", "123", {a}, history, error));
        CHECK(Read(attempts) == "damaged");
        // Remove only the named files this executable created.
        std::filesystem::remove(attempts);
        std::filesystem::remove(RuntimePaths().attempts);
        std::filesystem::remove(playlist);
        auto temporary = playlist;
        temporary += ".tmp";
        std::filesystem::remove(temporary);
        std::filesystem::remove(RuntimePaths().playlists);
        std::filesystem::remove(root);
        std::cout << checks
                  << " platform checks passed (Unicode paths, repeated saves, locked-file preservation, history).\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
