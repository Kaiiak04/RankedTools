#pragma once

#include "recommendation.hpp"

#include <string>
#include <vector>
#include <cstddef>

namespace rankedpractice {

// Per worker thread, including failed requests; never shared between services.
struct HttpStatistics {
    std::size_t requests{0};
    long connections{0};
    double seconds{0.0};
};
HttpStatistics GetHttpStatistics();

struct StarRange {
    double minStars{0.0};
    double maxStars{20.0};
    bool valid{false};
};

struct ClanPlaylistRecommendations {
    std::string clanTag;
    std::vector<MapEntry> entries;
    std::string image;
    std::string iconWarning;
};

// Owned by one refresh worker; never shared with the settings UI.
struct ScoreHistory {
    bool loaded{false};
    std::string playerId;
    RatingSystem system{RatingSystem::BeatLeader};
    std::vector<MapEntry> scores;
    std::vector<Attempt> attempts;
    std::string attemptWarning;
    bool importAttempts{true};
};

bool FetchRecommendations(const std::string& service,
                          const std::string& playerId,
                          ListKind kind,
                          int limit,
                          std::vector<MapEntry>& entries,
                          std::string& error,
                          double minClearRate,
                          StarRange* beatLeaderNotPlayedRange = nullptr,
                          ScoreHistory* history = nullptr);

bool FetchBeatLeaderNotPlayedStarRange(const std::string& playerId,
                                       double minClearRate,
                                       StarRange& range,
                                       std::string& error,
                                       ScoreHistory* history = nullptr);

bool FetchClanToConquer(const std::string& playerId,
                        bool allClans,
                        double minStars,
                        double maxStars,
                        int limit,
                        std::vector<ClanPlaylistRecommendations>& clanPlaylists,
                        std::string& error);

} // namespace rankedpractice
