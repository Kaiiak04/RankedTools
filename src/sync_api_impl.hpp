bool FetchSyncAccountsInternal(const std::vector<SyncAccount>& requested, int required,
    std::vector<MapEntry>& entries, std::string& error, const HttpGetter& get = HttpGet, bool pace = true) {
    entries.clear(); error.clear();
    std::vector<SyncAccount> accounts;
    std::unordered_set<std::string> services;
    std::unordered_set<ScoreHistory*> histories;
    for (const auto& account : requested) {
        if (account.playerId.empty()) continue;
        if (std::find(kLeaderboardServices.begin(), kLeaderboardServices.end(), account.service) == kLeaderboardServices.end() ||
            !services.insert(account.service).second || !IsCanonicalAccountId(account.playerId) ||
            (account.history && !histories.insert(account.history).second)) {
            error = "Sync has invalid or repeated leaderboard accounts."; return false;
        }
        accounts.push_back(account);
    }
    if (required < 2 || required > static_cast<int>(kLeaderboardServices.size()) || accounts.size() < static_cast<size_t>(required)) {
        error = "Select at least " + std::to_string(required) + " leaderboard accounts for Required Sync Scoreboards."; return false;
    }
    std::vector<ScoreHistory> locals(accounts.size());
    std::vector<SyncProviderData> data(accounts.size());
    std::vector<std::string> errors(accounts.size());
    std::vector<bool> succeeded(accounts.size());
    auto fetch = [&](size_t i) {
        const auto& account = accounts[i];
        auto& history = account.history ? *account.history : locals[i];
        auto& failure = errors[i]; auto& provider = data[i]; provider.service = account.service;
        if (account.service == "AccSaber") {
            if (!LoadAccHistory(account.playerId, history, failure, get, pace)) return false;
            provider.catalog = history.accCatalog;
        } else {
            const auto system = account.service == "BeatLeader" ? RatingSystem::BeatLeader : RatingSystem::ScoreSaber;
            if (!LoadScoreHistory(account.playerId, system, history, failure, get, pace, false)) return false;
            const bool bl = system == RatingSystem::BeatLeader;
            const auto getter = bl ? get : DefaultRealmCompatibleGet(get);
            if (!FetchPages<MapEntry>([&](int page) {
                return std::string(bl ? "https://api.beatleader.com/maps?type=ranked&count=100&sortBy=stars&order=desc&page=" :
                    "https://scoresaber.com/api/v2/leaderboards?status=RANKED&realmId=1&limit=100&sortBy=stars&sortDirection=desc&page=") + std::to_string(page);
            }, provider.catalog, failure, [&](const auto& body, auto& rows, auto& message, auto* info) {
                return ParseSyncPage(body, system, false, rows, message, info);
            }, getter, pace)) return false;
        }
        if (!history.syncComplete) { failure = "Posted-score identities are incomplete. " + history.syncError; return false; }
        provider.scores = history.syncScores; return true;
    };
    // At most two providers fetch at once. Always join before examining output.
    for (size_t i = 0; i < accounts.size(); i += 2) {
        std::future<bool> first;
        try { first = std::async(std::launch::async, fetch, i); }
        catch (const std::system_error&) { succeeded[i] = fetch(i); }
        if (i + 1 < accounts.size()) succeeded[i + 1] = fetch(i + 1);
        if (first.valid()) succeeded[i] = first.get();
    }
    for (size_t i = 0; i < accounts.size(); ++i) if (!succeeded[i]) {
        if (!error.empty()) error += "; ";
        error += accounts[i].service + ": " + errors[i];
    }
    if (!error.empty()) return false;
    entries = SelectSyncPlaylist(data, required); return true;
}

bool FetchSyncPlaylistInternal(const std::string& blId, const std::string& ssId,
    std::vector<MapEntry>& entries, std::string& error, ScoreHistory* blCached, ScoreHistory* ssCached,
    const HttpGetter& get = HttpGet, bool pace = true) {
    return FetchSyncAccountsInternal({{"BeatLeader", blId, blCached}, {"ScoreSaber", ssId, ssCached}},
        2, entries, error, get, pace);
}
