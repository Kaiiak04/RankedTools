#pragma once

#include <array>
#include <exception>
#include <functional>
#include <future>
#include <string>
#include <system_error>
#include <utility>

namespace rankedpractice {

struct RefreshTaskResult {
    bool succeeded{true};
    std::string error;
};

// BeatLeader/clans share one task and history; ScoreSaber owns the other.
// Always join both tasks before reporting completion or releasing refreshInProgress.
inline std::array<RefreshTaskResult, 2> RunRefreshTasks(
        const std::function<bool()>& beatLeader, const std::function<bool()>& scoreSaber) {
    const auto run = [](const std::function<bool()>& task) -> RefreshTaskResult {
        if (!task) return {};
        try { return {task(), {}}; }
        catch (const std::exception& exception) { return {false, exception.what()}; }
        catch (...) { return {false, "Unknown error."}; }
    };
    if (!beatLeader || !scoreSaber) return {run(beatLeader), run(scoreSaber)};
    std::future<RefreshTaskResult> second;
    try {
        second = std::async(std::launch::async, run, std::cref(scoreSaber));
    } catch (const std::system_error&) {
        // A device unable to start another thread can still refresh sequentially.
        return {run(beatLeader), run(scoreSaber)};
    }
    auto first = run(beatLeader);
    return {std::move(first), second.get()};
}

} // namespace rankedpractice
