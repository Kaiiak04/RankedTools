bool FetchSyncPlaylistInternal(const std::string& blId, const std::string& ssId,
    std::vector<MapEntry>& entries, std::string& error, ScoreHistory* blCached, ScoreHistory* ssCached,
    const HttpGetter& get = HttpGet, bool pace = true) {
    entries.clear(); error.clear();
    if (!IsDigits(blId) || !IsDigits(ssId)) {
        error = "Select both a BeatLeader account and a ScoreSaber account for Sync."; return false;
    }
    ScoreHistory blLocal, ssLocal;
    auto& bl = blCached ? *blCached : blLocal;
    auto& ss = ssCached ? *ssCached : ssLocal;
    std::vector<MapEntry> blCatalog, ssCatalog;
    std::string blError, ssError;
    auto fetch = [&](RatingSystem system, const std::string& id, ScoreHistory& history,
                      std::vector<MapEntry>& catalog, std::string& failure) {
        if (!LoadScoreHistory(id, system, history, failure, get, pace, false)) return false;
        if (!history.syncComplete) {
            failure = history.syncError.empty() ? "Posted-score identities were incomplete." : history.syncError;
            return false;
        }
        const bool beatLeader = system == RatingSystem::BeatLeader;
        auto getter = beatLeader ? get : DefaultRealmCompatibleGet(get);
        return FetchPages<MapEntry>([&](int page) {
            return std::string(beatLeader ?
                "https://api.beatleader.com/maps?type=ranked&count=100&sortBy=stars&order=desc&page=" :
                "https://scoresaber.com/api/v2/leaderboards?status=RANKED&realmId=1&limit=100&sortBy=stars&sortDirection=desc&page=") + std::to_string(page);
        }, catalog, failure, [&](const std::string& body, std::vector<MapEntry>& rows, std::string& message, PageInfo* info) {
            return ParseSyncPage(body, system, false, rows, message, info);
        }, getter, pace);
    };
    // Provider histories are no longer in use by the personal/clan workers.
    // Join both even after a failure; never publish a partial intersection.
    auto blTask = std::async(std::launch::async, [&] { return fetch(RatingSystem::BeatLeader, blId, bl, blCatalog, blError); });
    const bool ssOk = fetch(RatingSystem::ScoreSaber, ssId, ss, ssCatalog, ssError);
    const bool blOk = blTask.get();
    if (!blOk || !ssOk) {
        error = (!blOk ? "BeatLeader: " + blError : "") + (!blOk && !ssOk ? "; " : "") + (!ssOk ? "ScoreSaber: " + ssError : "");
        return false;
    }
    entries = SelectSyncPlaylist(blCatalog, ssCatalog, bl.syncScores, ss.syncScores);
    return true;
}
