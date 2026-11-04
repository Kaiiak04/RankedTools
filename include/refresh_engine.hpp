#pragma once

#include "recommendation_api.hpp"
#include "refresh_settings.hpp"
#include <functional>

namespace rankedpractice {
struct RefreshObserver {
    // Called on provider workers. Hosts marshal UI work to their main thread.
    std::function<void(const std::string &)> status;
    std::function<void(const std::string &)> log;
    std::function<void(const std::string &, ListKind, const std::vector<MapEntry> &)> playlist;
    std::function<void(const std::vector<ClanPlaylistRecommendations> &)> clans;
    std::function<void(const std::string &, const std::vector<MapEntry> &, const std::string &)> ability;
    std::function<void(const std::string &)> syncError;
};
struct RefreshResult {
    bool succeeded{false};
    bool filesChanged{false};
};
// Blocking, game-independent orchestration. Settings and provider histories are
// owned by this invocation. The host is responsible for the single-refresh gate.
RefreshResult RunRefresh(const RefreshSettings &settings, const RefreshObserver &observer);
} // namespace rankedpractice
