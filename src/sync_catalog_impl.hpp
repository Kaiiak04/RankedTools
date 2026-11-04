// Included inside recommendation_api.cpp's private namespace. Sync deliberately
// bypasses recommendation star bands, PP/clearability filters and song limits.
bool SyncIdentity(const Context& context) {
    return context.hash.size() == 40 &&
        std::all_of(context.hash.begin(), context.hash.end(), [](unsigned char c) { return std::isxdigit(c); }) &&
        !context.characteristic.empty() &&
        (context.difficulty == "Easy" || context.difficulty == "Normal" || context.difficulty == "Hard" ||
         context.difficulty == "Expert" || context.difficulty == "ExpertPlus");
}

bool SyncBuiltInScore(const rapidjson::Value& leaderboard, RatingSystem system, const Context& context) {
    if (context.ranked || context.hash.empty()) return false;
    // Official-song scores are returned alongside custom maps. Their level IDs
    // cannot match the SHA-1 hashes in either ranked custom-map catalog.
    if (system == RatingSystem::ScoreSaber) {
        return Lower(context.hash).starts_with("ost_") && context.hash.size() > 4;
    }
    if (!leaderboard.HasMember("song") || !leaderboard["song"].IsObject()) return false;
    std::string id;
    if (!ReadString(leaderboard["song"], "id", id) || id != context.hash || id.starts_with("custom_level_")) return false;
    // BeatLeader custom songs have a BeatSaver ID separate from their hash.
    // Do not silently discard an empty, truncated or otherwise invalid hash.
    return std::any_of(id.begin(), id.end(), [](unsigned char c) { return !std::isxdigit(c); }) &&
        std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; });
}

bool ParseSyncPage(const std::string& body, RatingSystem system, bool scores,
                   std::vector<MapEntry>& entries, std::string& error, PageInfo* info) {
    rapidjson::Document document;
    document.Parse(body.c_str());
    if (document.HasParseError() || !document.IsObject()) {
        error = "Sync received an invalid API page."; return false;
    }
    if (system == RatingSystem::ScoreSaber && !HasDefaultScoreSaberRealm(document)) {
        error = "Sync received a non-default ScoreSaber realm."; return false;
    }
    const rapidjson::Value* rows = nullptr;
    for (const char* key : {"data", "playerScores", "leaderboards"})
        if (document.HasMember(key) && document[key].IsArray()) { rows = &document[key]; break; }
    if (!rows) { error = "Sync received an unexpected API page format."; return false; }
    if (info) {
        *info = {};
        info->rawRows = rows->Size();
        if (document.HasMember("metadata") && document["metadata"].IsObject()) {
            const auto& metadata = document["metadata"];
            ReadNumber(metadata, "total", info->total);
            ReadNumber(metadata, "totalItems", info->total);
            ReadNumber(metadata, "totalPages", info->totalPages);
            ReadNumber(metadata, "page", info->page);
        }
    }
    std::vector<MapEntry> parsed;
    auto emit = [&](Context context) {
        if (!SyncIdentity(context)) {
            error = "Sync could not identify a custom chart (hash, characteristic or difficulty); previous playlist preserved.";
            return false;
        }
        // Presence is independent of PP, accuracy, NF, stars and applicability.
        context.applicable = true;
        if (context.songName.empty()) context.songName = context.hash;
        context.hash = Lower(context.hash);
        AddIfComplete(context, parsed, true);
        return true;
    };
    for (const auto& row : rows->GetArray()) {
        if (!row.IsObject()) { error = "Sync received a malformed chart row."; return false; }
        if (scores && !(row.HasMember("baseScore") && row["baseScore"].IsNumber()) &&
            !(row.HasMember("score") && row["score"].IsObject())) {
            error = "Sync received a row without a posted score."; return false;
        }
        const auto& leaderboard = row.HasMember("leaderboard") && row["leaderboard"].IsObject() ? row["leaderboard"] : row;
        Context context;
        UpdateContext(leaderboard, context);
        for (const char* key : {"song", "map", "realm"})
            if (leaderboard.HasMember(key) && leaderboard[key].IsObject()) UpdateContext(leaderboard[key], context);
        if (!scores && system == RatingSystem::BeatLeader) {
            // /maps?type=ranked selects songs, not every nested difficulty.
            if (!leaderboard.HasMember("difficulties") || !leaderboard["difficulties"].IsArray()) {
                error = "Sync received a BeatLeader song without chart statuses."; return false;
            }
            for (const auto& chart : leaderboard["difficulties"].GetArray()) {
                if (!chart.IsObject() || !chart.HasMember("status") || !chart["status"].IsInt()) {
                    error = "Sync could not verify a BeatLeader chart's ranked status."; return false;
                }
                if (chart["status"].GetInt() != 3) continue;
                auto chartContext = context;
                UpdateContext(chart, chartContext);
                if (!emit(chartContext)) return false;
            }
        } else {
            if (leaderboard.HasMember("difficulty") && leaderboard["difficulty"].IsObject())
                UpdateContext(leaderboard["difficulty"], context);
            if (scores && SyncBuiltInScore(leaderboard, system, context)) continue;
            if (!scores) {
                const bool hasStatus = leaderboard.HasMember("ranked") || leaderboard.HasMember("leaderboardStatus") ||
                    (leaderboard.HasMember("realm") && leaderboard["realm"].IsObject() && leaderboard["realm"].HasMember("leaderboardStatus"));
                if (!hasStatus) { error = "Sync could not verify a ScoreSaber chart's ranked status."; return false; }
                if (!context.ranked) continue;
            }
            if (!emit(context)) return false;
        }
    }
    entries.insert(entries.end(), std::make_move_iterator(parsed.begin()), std::make_move_iterator(parsed.end()));
    return true;
}

std::vector<MapEntry> SelectSyncPlaylist(const std::vector<MapEntry>& beatLeaderCatalog,
    const std::vector<MapEntry>& scoreSaberCatalog, const std::vector<MapEntry>& beatLeaderScores,
    const std::vector<MapEntry>& scoreSaberScores) {
    std::unordered_set<std::string> ssRanked, blPlayed, ssPlayed, emitted;
    for (const auto& chart : scoreSaberCatalog) if (chart.ranked) ssRanked.insert(EntryKey(chart));
    for (const auto& score : beatLeaderScores) blPlayed.insert(EntryKey(score));
    for (const auto& score : scoreSaberScores) ssPlayed.insert(EntryKey(score));
    std::unordered_map<std::string, size_t> songIndices;
    std::vector<MapEntry> result;
    for (const auto& chart : beatLeaderCatalog) {
        if (!chart.ranked || chart.difficulties.empty()) continue;
        const auto key = EntryKey(chart);
        const bool bl = blPlayed.contains(key), ss = ssPlayed.contains(key);
        if (!ssRanked.contains(key) || bl == ss || !emitted.insert(key).second) continue;
        const auto hash = Lower(chart.hash);
        auto [it, inserted] = songIndices.emplace(hash, result.size());
        if (inserted) {
            auto song = chart;
            song.hash = hash;
            song.difficulties.clear(); song.syncTargets.clear();
            result.push_back(std::move(song));
        }
        auto& song = result[it->second];
        const auto& difficulty = chart.difficulties.front();
        song.difficulties.push_back(difficulty);
        song.syncTargets.push_back({difficulty.characteristic, difficulty.name, bl ? "ScoreSaber" : "BeatLeader"});
    }
    std::sort(result.begin(), result.end(), [](const MapEntry& a, const MapEntry& b) {
        const auto an = Lower(a.songName), bn = Lower(b.songName);
        return an == bn ? a.hash < b.hash : an < bn;
    });
    return result;
}
