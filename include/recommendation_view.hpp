#pragma once

#include "recommendation.hpp"
#include "HMUI/ViewController.hpp"

#include <string>
#include <vector>

namespace rankedpractice {
struct ClanPlaylistRecommendations;

void BuildRecommendationView(HMUI::ViewController* controller);
void SetViewStatus(const std::string& status);
void ConfigureRecommendationPreviews(double minClearRate, bool simpleMode,
                                      bool enableBeatLeader, bool enableScoreSaber,
                                      bool enableClans = false);
void PublishClanPreviews(const std::vector<ClanPlaylistRecommendations>& playlists);
void PublishRecommendationPreview(const std::string& service,
                                  ListKind kind,
                                  const std::vector<MapEntry>& entries);
void PublishAbilityPreview(const std::string& service, const std::vector<MapEntry>& scores,
                           const std::string& error = {});

} // namespace rankedpractice
