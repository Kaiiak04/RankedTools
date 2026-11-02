// Uses the production parser, selector and complete paginated HTTP pipeline.
std::string SyncBlRow(int id, const std::string& mode = "Standard", const std::string& difficulty = "ExpertPlus", int status = 3) {
    return "{\"hash\":\"" + Hash(id) + "\",\"name\":\"Song " + std::to_string(id) +
        "\",\"difficulties\":[{\"difficultyName\":\"" + difficulty + "\",\"modeName\":\"" + mode +
        "\",\"status\":" + std::to_string(status) + ",\"stars\":0,\"applicable\":false}]}";
}
std::string SyncSsRow(int id, const std::string& mode = "Standard", const std::string& difficulty = "ExpertPlus") {
    return "{\"map\":{\"hash\":\"" + Hash(id) + "\",\"songName\":\"Song\"},\"difficulty\":{\"rawDifficulty\":\"_" +
        difficulty + "_Solo" + mode + "\",\"gameMode\":\"Solo" + mode +
        "\"},\"realm\":{\"realmId\":1,\"leaderboardStatus\":\"RANKED\",\"stars\":0}}";
}
ScoreHistory SyncHistory(RatingSystem system, const std::string& id, const std::vector<MapEntry>& scores) {
    ScoreHistory result;
    result.loaded = result.syncComplete = true;
    result.playerId = id; result.system = system; result.scores = result.syncScores = scores;
    return result;
}
void SyncPlaylistContract() {
    std::vector<MapEntry> bl, ss;
    std::string error;
    PageInfo info;
    for (const auto& provider : {std::pair{"beatleader-scores", RatingSystem::BeatLeader},
                                 std::pair{"scoresaber-scores", RatingSystem::ScoreSaber}}) {
        std::vector<MapEntry> posted;
        CHECK(ParseSyncPage(ReadFile(std::string("tests/fixtures/") + provider.first + ".json"), provider.second, true, posted, error, &info));
        CHECK(posted.size() == info.rawRows); // Real nested PB schemas retain every chart.
    }
    for (const auto& provider : {std::pair{"beatleader", RatingSystem::BeatLeader},
                                 std::pair{"scoresaber", RatingSystem::ScoreSaber}}) {
        const auto source = ReadFile(std::string("tests/fixtures/sync-built-in-") + provider.first + ".json");
        std::vector<MapEntry> posted;
        CHECK(ParseSyncPage(source, provider.second, true, posted, error, &info));
        CHECK(info.rawRows == 3 && posted.empty()); // Complete history pages may contain only OST scores.
        rapidjson::Document mixed; mixed.Parse(source.c_str());
        auto& rows = mixed[provider.second == RatingSystem::BeatLeader ? "data" : "playerScores"];
        rapidjson::Document custom; custom.Parse(Chart(55, 0, true, "NF").c_str());
        rows.PushBack(rapidjson::Value(custom, mixed.GetAllocator()), mixed.GetAllocator());
        rapidjson::StringBuffer body; rapidjson::Writer<rapidjson::StringBuffer> writer(body); mixed.Accept(writer);
        CHECK(ParseSyncPage(body.GetString(), provider.second, true, posted, error, &info));
        CHECK(info.rawRows == 4 && posted.size() == 1 && posted[0].hash == Hash(55));
        // Known built-ins are skipped, but damaged custom rows still abort the page.
        rows[3]["hash"].SetString("invalid-custom-hash", mixed.GetAllocator());
        rows[3].AddMember("ranked", false, mixed.GetAllocator());
        body.Clear(); writer.Reset(body); mixed.Accept(writer);
        posted.clear();
        CHECK(!ParseSyncPage(body.GetString(), provider.second, true, posted, error, &info) && posted.empty());
    }
    for (const auto& mode : {"Standard", "OneSaber", "NoArrows", "90Degree", "360Degree", "Lawless", "CustomMode"}) {
        CHECK(ParseSyncPage(Page(1, 1, {SyncBlRow(1, mode)}), RatingSystem::BeatLeader, false, bl, error, &info));
        CHECK(ParseSyncPage(Page(1, 1, {SyncSsRow(1, mode)}), RatingSystem::ScoreSaber, false, ss, error, &info));
    }
    CHECK(bl.size() == 7 && ss.size() == 7); // Zero stars/inapplicable charts still have identities.
    auto result = SelectSyncPlaylist(bl, ss, bl, {});
    CHECK(result.size() == 1 && result[0].difficulties.size() == 7 && result[0].syncTargets.size() == 7);
    for (const auto& target : result[0].syncTargets) CHECK(target.missingService == "ScoreSaber");
    CHECK(SelectSyncPlaylist(bl, ss, bl, ss).empty());
    CHECK(SelectSyncPlaylist(bl, ss, {}, {}).empty());
    auto nf = bl.front(); nf.noFail = true; nf.pp = nf.accuracy = 0;
    result = SelectSyncPlaylist(bl, ss, {nf}, {ss[1]});
    CHECK(result.size() == 1 && result[0].difficulties.size() == 2);
    CHECK(result[0].syncTargets[0].missingService == "ScoreSaber");
    CHECK(result[0].syncTargets[1].missingService == "BeatLeader");
    auto different = ss.front(); different.difficulties[0].name = "Hard";
    CHECK(SelectSyncPlaylist({bl.front()}, {different}, {nf}, {}).empty());
    different = ss.front(); different.difficulties[0].characteristic = "OneSaber";
    CHECK(SelectSyncPlaylist({bl.front()}, {different}, {nf}, {}).empty());
    different = ss.front(); different.hash = Hash(2);
    CHECK(SelectSyncPlaylist({bl.front()}, {different}, {nf}, {}).empty());
    auto uppercase = bl.front(); uppercase.hash = std::string(40, 'A');
    auto lowercase = uppercase; lowercase.hash = std::string(40, 'a');
    result = SelectSyncPlaylist({uppercase, uppercase}, {lowercase}, {lowercase}, {});
    CHECK(result.size() == 1 && result[0].difficulties.size() == 1);
    bl.clear();
    CHECK(ParseSyncPage(Page(1, 1, {SyncBlRow(2, "Standard", "ExpertPlus", 2)}), RatingSystem::BeatLeader, false, bl, error, &info));
    CHECK(bl.empty());
    for (const auto& bad : {"{}", "{\"data\":[{}]}", "{\"data\":[null]}", "{\"data\":[{\"hash\":\"bad\",\"difficulties\":[{}]}]}"}) {
        CHECK(!ParseSyncPage(bad, RatingSystem::BeatLeader, false, bl, error, &info));
        CHECK(bl.empty());
    }
    CHECK(!ParseSyncPage("{\"data\":[{}]}", RatingSystem::BeatLeader, true, bl, error, &info));
    CHECK(!ParseSyncPage("{\"data\":[{\"realmId\":2}]}", RatingSystem::ScoreSaber, false, bl, error, &info));
    CHECK(ParseSyncPage(Page(1, 1, {Chart(7, 0, true, "NF")}), RatingSystem::BeatLeader, true, bl, error, &info));
    CHECK(bl.size() == 1); // Posted NF is filled even at zero PP/stars.

    std::atomic<int> blScores{0}, ssScores{0}, blPages{0}, ssPages{0};
    auto get = [&](const std::string& url, std::string& body, std::string&) {
        const bool beatLeader = url.find("beatleader.com") != std::string::npos;
        const bool score = url.find("/scores?") != std::string::npos;
        (score ? (beatLeader ? blScores : ssScores) : (beatLeader ? blPages : ssPages)).fetch_add(1);
        const int page = std::stoi(url.substr(url.rfind("page=") + 5));
        std::vector<std::string> rows;
        if (score && !beatLeader) body = Page(page, 0, {});
        else {
            for (int id = (page - 1) * 100 + 1; id <= std::min(page * 100, 205); ++id)
                rows.push_back(score ? Chart(id, 0, true, "NF") : beatLeader ? SyncBlRow(id) : SyncSsRow(id));
            body = Page(page, 205, rows);
        }
        return true;
    };
    ScoreHistory blHistory, ssHistory;
    CHECK(FetchSyncPlaylistInternal("123", "456", result, error, &blHistory, &ssHistory, get, false));
    CHECK(result.size() == 205 && blHistory.syncScores.size() == 205);
    CHECK(blScores == 3 && ssScores == 1 && blPages == 3 && ssPages == 3);
    CHECK(FetchSyncPlaylistInternal("123", "456", result, error, &blHistory, &ssHistory, get, false));
    CHECK(result.size() == 205 && blScores == 3 && ssScores == 1); // Histories reused.
    auto builtInHistory = [&](const std::string& url, std::string& body, std::string& failure) {
        if (url.find("/scores?") == std::string::npos) return get(url, body, failure);
        const bool beatLeader = url.find("beatleader.com") != std::string::npos;
        if (url.ends_with("page=2")) { body = Page(2, 4, {Chart(55, 0, true, "NF")}); return true; }
        const auto source = ReadFile(std::string("tests/fixtures/sync-built-in-") + (beatLeader ? "beatleader" : "scoresaber") + ".json");
        rapidjson::Document document; document.Parse(source.c_str());
        if (beatLeader) document["metadata"]["total"].SetInt(4);
        rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> writer(buffer); document.Accept(writer);
        body = buffer.GetString(); return true;
    };
    ScoreHistory builtInBl, builtInSs;
    CHECK(FetchSyncPlaylistInternal("123", "456", result, error, &builtInBl, &builtInSs, builtInHistory, false));
    CHECK(builtInBl.syncComplete && builtInSs.syncComplete);
    CHECK(builtInBl.syncScores.size() == 1 && builtInSs.syncScores.empty());
    CHECK(result.size() == 1 && result[0].hash == Hash(55)); // An all-OST page must not terminate pagination early.
    CHECK(result[0].syncTargets[0].missingService == "ScoreSaber");
    CHECK(!FetchSyncPlaylistInternal("", "456", result, error, &blHistory, &ssHistory, get, false) && result.empty());
    auto failed = [&](const std::string& url, std::string& body, std::string& failure) {
        if (url.find("scoresaber.com/api/v2") != std::string::npos && url.ends_with("page=2")) {
            failure = "HTTP 503"; return false;
        }
        return get(url, body, failure);
    };
    CHECK(!FetchSyncPlaylistInternal("123", "456", result, error, &blHistory, &ssHistory, failed, false));
    CHECK(result.empty() && error.find("ScoreSaber") != std::string::npos);
    auto early = [&](const std::string& url, std::string& body, std::string& failure) {
        if (url.find("scoresaber.com/api/v2") != std::string::npos && url.ends_with("page=2")) {
            body = Page(2, 205, {}); return true;
        }
        return get(url, body, failure);
    };
    CHECK(!FetchSyncPlaylistInternal("123", "456", result, error, &blHistory, &ssHistory, early, false) && result.empty());
    blHistory.syncComplete = false; blHistory.syncError = "Unidentified posted chart";
    CHECK(!FetchSyncPlaylistInternal("123", "456", result, error, &blHistory, &ssHistory, get, false));
    CHECK(result.empty() && error.find("Unidentified") != std::string::npos);
    blHistory = SyncHistory(RatingSystem::BeatLeader, "123", {});
    ssHistory = SyncHistory(RatingSystem::ScoreSaber, "456", {});
    CHECK(FetchSyncPlaylistInternal("123", "456", result, error, &blHistory, &ssHistory, get, false) && result.empty());
    std::cout << "PASS: Sync exact-chart XOR, all characteristics, NF presence, unlimited complete pagination and failure preservation\n";
}
