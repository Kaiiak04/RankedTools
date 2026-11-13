#include <bit>

std::string AccFixture(const std::string& name) { return ReadFile("tests/fixtures/accsaber/" + name + ".json"); }
bool AccFixtureGet(const std::string& url, std::string& body, std::string& error) {
    if (url == std::string(kAccApi) + "/categories") body = AccFixture("categories");
    else if (url == std::string(kAccApi) + "/modifiers") body = AccFixture("modifiers");
    else if (url.starts_with(std::string(kAccApi) + "/maps?")) body = AccFixture(url.ends_with("page=0") ? "maps-0" : "maps-1");
    else if (url.find("/scores?") != std::string::npos && url.starts_with(kAccApi)) body = AccFixture("scores-0");
    else if (url.starts_with(std::string(kAccApi) + "/users/")) body = AccFixture("profile");
    else { error = "Unexpected fixture URL: " + url; return false; }
    return true;
}

void AccSaberContract() {
    CHECK(static_cast<int>(RatingSystem::ScoreSaber) == 0 && static_cast<int>(RatingSystem::BeatLeader) == 1);
    CHECK(AbilitySlot("BeatLeader") == 0 && AbilitySlot("ScoreSaber") == 1 && AbilitySlot("AccSaber/tech_acc") == 4);
    CHECK(NormalizeRequiredSyncScoreboards(-1) == 2 && NormalizeRequiredSyncScoreboards(4) == 3);
    CHECK(NormalizeAccSaberCategory("invalid") == "overall");
    AccountQuery query; AccountPage page; std::string error;
    CHECK(ParseAccountInput(AccountService::AccSaber, "https://www.accsaber.com/players/789?tab=scores", query, error));
    CHECK(query.value == "789" && AccountRequestUrl(AccountService::AccSaber, query, 1) == std::string(kAccApi) + "/users/789");
    CHECK(!ParseAccountInput(AccountService::AccSaber, "https://accsaber.com.evil/players/789", query, error));
    CHECK(!ParseAccountInput(AccountService::AccSaber, "https://accsaber.com/players/%2f789", query, error));
    CHECK(ParseAccountInput(AccountService::AccSaber, "789", query, error));
    CHECK(ParseAccountPage(AccountService::AccSaber, query, 1, AccFixture("profile"), page, error));
    CHECK(page.profiles[0].id == "76561198000000001"); // Merged IDs resolve to the canonical string.
    CHECK(ParseAccountInput(AccountService::AccSaber, "Acc Player", query, error));
    CHECK(AccountRequestUrl(AccountService::AccSaber, query, 1).ends_with("search=Acc%20Player&size=5&page=0"));
    CHECK(ParseAccountPage(AccountService::AccSaber, query, 1, AccFixture("account-search"), page, error));
    CHECK(page.profiles.size() == 1 && page.profiles[0].name == "Acc Player" && page.profiles[0].rank == 2 && !page.hasNext);
    CHECK(!ParseAccountPage(AccountService::AccSaber, query, 2, AccFixture("account-search"), page, error));

    ScoreHistory history; history.importAttempts = false;
    std::vector<MapEntry> result;
    CHECK(FetchAccRecommendationsInternal("789", "overall", ListKind::Combined, 16, 0, history, result, error, AccFixtureGet, false));
    CHECK(history.loaded && history.accCatalog.size() == 6 && history.scores.size() == 3 && history.syncScores.size() == 3);
    CHECK(result.size() == 1 && result[0].hash == Hash(5) && !result[0].isImprovement);
    CHECK(result[0].predictedPp > 760 && result[0].weightedPpGain > 0);
    const auto& c = history.accCategories[0];
    CHECK(AccSaberAp(.95, 0, c.scoreCurve) == 549);
    CHECK(AccSaberAp(.95, 7, c.scoreCurve) == 762.5);
    CHECK(AccSaberAp(1, 14, c.scoreCurve) == 3904);
    CHECK(AccSaberAp(-.1, 7, c.scoreCurve) == 0 && AccSaberAp(1.1, 7, c.scoreCurve) == 0);
    CHECK(std::abs(AccSaberPositionWeight(0, c.weightCurve) - 1) < 1e-12);
    CHECK(std::abs(AccSaberPositionWeight(15, c.weightCurve) - .1) < 1e-12);
    CHECK(std::abs(AccSaberPositionWeight(1, c.weightCurve) - .965) > .001);
    rapidjson::Document live; live.Parse(AccFixture("live-ap-reference").c_str());
    AccSaberCurve liveCurve;
    CHECK(!live.HasParseError() && AccCurve(live["curve"], false, liveCurve, error, AccFixtureGet));
    for (const auto& sample : live["cases"].GetArray())
        CHECK(std::abs(AccSaberAp(sample["accuracy"].GetDouble(), sample["complexity"].GetDouble(), liveCurve) - sample["ap"].GetDouble()) <= .0000011);
    CHECK(HasAccSaberEvidence(history.scores, "true_acc") && HasAccSaberEvidence(history.scores, "standard_acc"));
    CHECK(!HasAccSaberEvidence(history.scores, "tech_acc"));
    auto chart = BuildAccSaberAbilityChart(history.scores, "standard_acc");
    CHECK(chart.complexityAxis && chart.scores.size() == 1 && chart.minObservedStars == 0);
    CHECK(RenderAbilityChart(chart).pixels.size() == 1024 * 576 * 4);
    // Every category uses the shared 30-day sigmoid, while its complexity
    // observations remain independent from the other category fits.
    const double now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::vector<MapEntry> categoryTraining;
    int scoreId = 12000;
    for (const auto& code : {"true_acc", "standard_acc", "tech_acc"}) {
        for (int i = 0; i < 3; ++i) {
            auto entry = history.scores[0]; entry.hash = Hash(scoreId++);
            entry.categoryCode = code; entry.noFail = false; entry.modifiers.clear();
            entry.stars = entry.difficulties[0].stars = i * 4.; entry.modifiedStars = 0;
            entry.difficulties[0].complexity = entry.stars;
            entry.accuracy = (entry.categoryCode == "true_acc" ? .995 : entry.categoryCode == "standard_acc" ? .985 : .975) - .015 * i;
            entry.pp = 100; entry.timepost = now - i * 30 * 86400.;
            categoryTraining.push_back(entry);
        }
    }
    for (const auto& code : {"true_acc", "standard_acc", "tech_acc"}) {
        const auto categoryChart = BuildAccSaberAbilityChart(categoryTraining, code);
        const AccuracyCurve expected(AccCategoryScores(categoryTraining, code, true), RatingSystem::AccSaber, true);
        CHECK(categoryChart.complexityAxis && categoryChart.scores.size() == 3 && categoryChart.minObservedStars == 0);
        CHECK(std::abs(categoryChart.scores[1].weight / categoryChart.scores[0].weight - std::exp(-1.0)) < 1e-12);
        for (const auto& point : categoryChart.curve) CHECK(std::abs(point.accuracy - expected.Predict(point.stars)) < 1e-12);
    }
    const auto isolatedBefore = BuildAccSaberAbilityChart(categoryTraining, "true_acc");
    for (auto& entry : categoryTraining) if (entry.categoryCode != "true_acc") entry.accuracy = .60;
    const auto isolatedAfter = BuildAccSaberAbilityChart(categoryTraining, "true_acc");
    CHECK(isolatedBefore.curve.size() == isolatedAfter.curve.size());
    for (size_t i = 0; i < isolatedBefore.curve.size(); ++i)
        CHECK(std::abs(isolatedBefore.curve[i].accuracy - isolatedAfter.curve[i].accuracy) < 1e-12);
    auto speed = history.scores; speed[0].modifiers = "FS";
    CHECK(!HasAccSaberEvidence(speed, "true_acc")); // The other true-acc score is NF.
    CHECK(FetchAccRecommendationsInternal("789", "true_acc", ListKind::NotPlayed, 1, 0, history, result, error, AccFixtureGet, false));
    CHECK(result.size() == 1 && result[0].categoryLabel == "True Acc");
    CHECK(FetchAccRecommendationsInternal("789", "true_acc", ListKind::ToImprove, 16, 0, history, result, error, AccFixtureGet, false));
    CHECK(result.empty()); // Matching the existing unmodified score cannot replace its PB.
    CHECK(!FetchAccRecommendationsInternal("789", "tech_acc", ListKind::Combined, 16, 0, history, result, error, AccFixtureGet, false));
    CHECK(result.empty() && error.find("eligible scores") != std::string::npos);

    auto improved = history.scores;
    improved[0].accuracy = .99; improved[0].scoreNoMods = 99000; improved[0].pp = AccSaberAp(.99, 7, c.scoreCurve);
    auto picks = SelectAccSaberRecommendations(history.accCatalog, improved, history.accCategories, {}, "true_acc", ListKind::Combined, 16, 0);
    const auto nf = std::find_if(picks.begin(), picks.end(), [](const auto& entry) { return entry.hash == Hash(2); });
    CHECK(nf != picks.end() && !nf->isImprovement && nf->currentPp == history.scores[2].pp && nf->weightedPpGain > 0);
    auto otherCategory = history.scores; otherCategory[1].pp = 1000000;
    const auto before = SelectAccSaberRecommendations(history.accCatalog, history.scores, history.accCategories, {}, "true_acc", ListKind::Combined, 16, 0);
    const auto after = SelectAccSaberRecommendations(history.accCatalog, otherCategory, history.accCategories, {}, "true_acc", ListKind::Combined, 16, 0);
    CHECK(before.size() == 1 && after.size() == 1 && before[0].weightedPpGain == after[0].weightedPpGain);
    auto sameSong = history.accCatalog; auto extra = sameSong.back(); extra.leaderboardId = "extra"; extra.difficulties[0].name = "Hard"; sameSong.push_back(extra);
    CHECK(SelectAccSaberRecommendations(sameSong, history.scores, history.accCategories, {}, "overall", ListKind::Combined, 16, 0).size() == 1);
    Attempt failure; failure.id = "acc-failure"; failure.source = "local"; failure.hash = Hash(5); failure.difficulty = "ExpertPlus";
    failure.normal = true; failure.stars = 999; failure.outcome = AttemptOutcome::Fail;
    failure.timestamp = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto withFailure = SelectAccSaberRecommendations(history.accCatalog, history.scores, history.accCategories, {failure}, "true_acc", ListKind::Combined, 16, 0);
    CHECK(withFailure.size() == 1 && withFailure[0].nearbyFailures == 1 && withFailure[0].clearChance < before[0].clearChance);
    auto reweighted = history.accCatalog; reweighted.back().difficulties[0].complexity = 8;
    CHECK(SelectAccSaberRecommendations(reweighted, history.scores, history.accCategories, {}, "true_acc", ListKind::Combined, 16, 0)[0].predictedPp != before[0].predictedPp);

    const auto broken = [&](const std::string& url, std::string& body, std::string& failure) {
        if (url.find("/maps?") != std::string::npos && url.ends_with("page=1")) { failure = "HTTP 503"; return false; }
        return AccFixtureGet(url, body, failure);
    };
    ScoreHistory incomplete; incomplete.importAttempts = false;
    CHECK(!LoadAccHistory("789", incomplete, error, broken, false) && !incomplete.loaded);
    const auto changedTotals = [&](const std::string& url, std::string& body, std::string& failure) {
        if (!AccFixtureGet(url, body, failure)) return false;
        if (url.find("/maps?") != std::string::npos && url.ends_with("page=1")) {
            auto pos = body.find("\"totalElements\": 5"); CHECK(pos != std::string::npos); body.replace(pos, 18, "\"totalElements\": 6");
        }
        return true;
    };
    CHECK(!LoadAccHistory("789", incomplete, error, changedTotals, false));
    const auto partial = [&](const std::string& url, std::string& body, std::string& failure) {
        if (!AccFixtureGet(url, body, failure)) return false;
        if (url.find("/scores?") != std::string::npos) {
            const auto pos = body.rfind("\"partial\": false"); body.replace(pos, 16, "\"partial\": true");
        }
        return true;
    };
    CHECK(LoadAccHistory("789", incomplete, error, partial, false) && incomplete.syncScores.size() == 2);
    const auto noRatings = [&](const std::string& url, std::string& body, std::string& failure) {
        if (!AccFixtureGet(url, body, failure)) return false;
        if (url.find("/scores?") != std::string::npos) {
            rapidjson::Document doc; doc.Parse(body.c_str());
            for (auto& row : doc["content"].GetArray()) { row.RemoveMember("ap"); row.RemoveMember("accuracy"); row.RemoveMember("scoreNoMods"); }
            rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> writer(buffer); doc.Accept(writer); body = buffer.GetString();
        }
        return true;
    };
    ScoreHistory identitiesOnly;
    CHECK(LoadAccHistory("789", identitiesOnly, error, noRatings, false) && identitiesOnly.syncScores.size() == 3);
    CHECK(!LoadAccHistory("789", identitiesOnly, error, noRatings, false, true)); // Personal predictions still require complete ratings.
    auto sparse = history; sparse.scores.clear();
    CHECK(!FetchAccRecommendationsInternal("789", "overall", ListKind::Combined, 16, 0, sparse, result, error, AccFixtureGet, false));
    std::cout << "PASS: AccSaber accounts, source-contract API pages, category AP/complexity models, strict PBs, sparse guidance and failure preservation\n";
}

void ThreeServiceSyncContract() {
    auto chart = Score(77); chart.noFail = true;
    for (unsigned ranked = 0; ranked < 8; ++ranked) for (unsigned posted = 0; posted < 8; ++posted) for (int required : {2, 3}) {
        std::vector<SyncProviderData> data;
        for (unsigned i = 0; i < 3; ++i) data.push_back({std::string(kLeaderboardServices[i]),
            ranked & (1u << i) ? std::vector{chart} : std::vector<MapEntry>{},
            posted & (1u << i) ? std::vector{chart} : std::vector<MapEntry>{}});
        const auto result = SelectSyncPlaylist(data, required);
        const bool expected = std::popcount(ranked) >= required && (posted & ranked) != 0 && (posted & ranked) != ranked;
        CHECK(result.size() == static_cast<size_t>(expected));
        if (expected) CHECK(result[0].difficulties.size() == 1 && result[0].syncTargets.size() == std::popcount(ranked & ~posted));
    }
    auto bl = SyncHistory(RatingSystem::BeatLeader, "123", {Score(5)});
    ScoreHistory acc; std::vector<MapEntry> result; std::string error;
    const auto get = [&](const std::string& url, std::string& body, std::string& failure) {
        if (url.starts_with("https://api.beatleader.com/maps?")) { body = Page(1, 2, {SyncBlRow(1), SyncBlRow(5)}); return true; }
        return AccFixtureGet(url, body, failure);
    };
    CHECK(FetchSyncAccountsInternal({{"BeatLeader", "123", &bl}, {"AccSaber", "789", &acc}}, 2, result, error, get, false));
    CHECK(result.size() == 2 && result[0].syncTargets[0].missingService == "BeatLeader" && result[1].syncTargets[0].missingService == "AccSaber");
    CHECK(!FetchSyncAccountsInternal({{"BeatLeader", "123", &bl}, {"AccSaber", "789", &acc}}, 3, result, error, get, false));
    CHECK(result.empty());
    ScoreHistory firstAccRefresh;
    bool unavailableOnce = true;
    const auto transient = [&](const std::string& url, std::string& body, std::string& failure) {
        if (unavailableOnce && url.starts_with(std::string(kAccApi) + "/maps?")) {
            unavailableOnce = false; failure = "HTTP 503"; return false;
        }
        return get(url, body, failure);
    };
    CHECK(!FetchSyncAccountsInternal({{"BeatLeader", "123", &bl}, {"AccSaber", "789", &firstAccRefresh}}, 2, result, error, transient, false));
    CHECK(result.empty() && !firstAccRefresh.loaded && error.find("AccSaber") != std::string::npos);
    CHECK(error.find("HTTP 503") != std::string::npos && error.find("/maps?") != std::string::npos && error.find("page=0") != std::string::npos);
    CHECK(FetchSyncAccountsInternal({{"BeatLeader", "123", &bl}, {"AccSaber", "789", &firstAccRefresh}}, 2, result, error, transient, false));
    CHECK(result.size() == 2 && error.empty());
    ScoreHistory noAccScores;
    const auto emptyAcc = [&](const std::string& url, std::string& body, std::string& failure) {
        if (url.starts_with(kAccApi) && url.find("/scores?") != std::string::npos) {
            body = R"({"content":[],"number":0,"totalElements":0,"totalPages":0})"; return true;
        }
        return get(url, body, failure);
    };
    CHECK(FetchSyncAccountsInternal({{"BeatLeader", "123", &bl}, {"AccSaber", "789", &noAccScores}}, 2, result, error, emptyAcc, false));
    CHECK(noAccScores.loaded && noAccScores.syncComplete && noAccScores.syncScores.empty());
    CHECK(result.size() == 1 && result[0].hash == Hash(5) && result[0].syncTargets[0].missingService == "AccSaber");
    const auto fail = [&](const std::string& url, std::string& body, std::string& failure) {
        if (url.starts_with("https://api.beatleader.com/")) { failure = "HTTP 429"; return false; }
        return AccFixtureGet(url, body, failure);
    };
    CHECK(!FetchSyncAccountsInternal({{"BeatLeader", "123", &bl}, {"AccSaber", "789", &acc}}, 2, result, error, fail, false));
    CHECK(result.empty() && error.find("BeatLeader") != std::string::npos);
    std::cout << "PASS: three-provider Sync rank/score matrix, multiple missing targets, selected-account thresholds and provider failure preservation\n";
}
