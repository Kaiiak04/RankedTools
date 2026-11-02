#pragma once

#include "recommendation.hpp"
#include "clan_recommendation.hpp"

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
// Diagnostic used by the packaged-engine smoke check; retains production TLS,
// certificate verification, protocol restrictions, and connection ownership.
bool ProbeHttps(const std::string& url, std::string& error);
std::string HttpsImplementation();

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
    bool hypothetical{false};
    bool legacy{false};
    double legacyMaxStars{0.0};
    std::size_t eligibleCharts{0};
    std::size_t checkedCharts{0};
    std::size_t failedCharts{0};
    std::size_t recheckedCharts{0};
    std::size_t learnedCharts{0};
    std::string warning;
    std::string error; // A failed clan preserves its previous playlist independently.
    std::size_t nextCursor{0};
    bool cursorReady{false};
    ClanSearchState searchState;
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
    // Unfiltered posted-score identities, including NF and inapplicable charts.
    // Collected alongside the ordinary profile without another network pass.
    bool syncComplete{false};
    std::vector<MapEntry> syncScores;
    std::string syncError;
};

bool FetchSyncPlaylist(const std::string& beatLeaderPlayerId, const std::string& scoreSaberPlayerId,
                       std::vector<MapEntry>& entries, std::string& error,
                       ScoreHistory* beatLeaderHistory = nullptr, ScoreHistory* scoreSaberHistory = nullptr);

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
                        int limit,
                        std::vector<ClanPlaylistRecommendations>& clanPlaylists,
                        std::string& error,
                        double minClearRate = 0.50,
                        ScoreHistory* history = nullptr,
                        bool legacy = false);

bool SaveClanExplorationCursor(const std::string& playerId, const std::string& clanTag,
                               std::size_t cursor, std::string& error);
bool SaveClanSearchState(const std::string& playerId, const std::string& clanTag,
                         const ClanSearchState& state, std::string& error);

} // namespace rankedpractice
