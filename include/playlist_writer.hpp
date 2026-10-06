#pragma once

#include "recommendation.hpp"

#include <string>
#include <vector>

namespace rankedpractice {

bool WritePlaylist(const std::string& service,
                   ListKind kind,
                   const std::vector<MapEntry>& entries,
                   std::string& error);

bool WriteClanPlaylist(const std::string& clanTag,
                       bool perClan,
                       const std::vector<MapEntry>& entries,
                       std::string& error,
                       const std::string& clanImage = {});

bool PruneClanPlaylists(const std::vector<std::string>& expectedFileNames,
                        std::string& error);

// Only the three exact generated filenames for this service are eligible for removal.
bool PruneLeaderboardPlaylists(const std::string& service,
                               const std::vector<std::string>& expectedFileNames,
                               std::string& error);

} // namespace rankedpractice
