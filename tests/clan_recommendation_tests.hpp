// Included by recommendation_tests.cpp to exercise production calculation and HTTP internals.
MapEntry ClanProjection(int id, double pp, double chance = .9) {
    auto entry = Score(id); entry.leaderboardId = std::to_string(id);
    entry.predictedPp = pp; entry.predictedAccuracy = .95; entry.clearChance = chance; entry.confidence = .8;
    return entry;
}
void PersonalizedClanContract() {
    CHECK(std::abs(WeightedClanPp({{"1", 500}, {"2", 450}, {"3", 400}}) - 1116) < 1e-8);
    ClanMapInput input; input.chart = ClanProjection(1, 350); input.rivalPp = 2000;
    input.clan.scores = {{"1", 500}, {"2", 450}, {"3", 400}}; input.clan.pp = WeightedClanPp(input.clan.scores);
    MapEntry result;
    CHECK(EvaluateClanImpact(input, "123", .5, result));
    CHECK(std::abs(result.clanImpact->gain - 179.2) < 1e-8);
    CHECK(!result.isImprovement && !result.clanImpact->captures);
    input.clan.scores.push_back({"123", 300, .9, 10}); input.clan.pp = WeightedClanPp(input.clan.scores);
    CHECK(EvaluateClanImpact(input, "123", .5, result));
    CHECK(std::abs(result.clanImpact->gain - 25.6) < 1e-8 && result.currentPp == 300);
    input.chart.predictedPp = 600;
    CHECK(EvaluateClanImpact(input, "123", .5, result));
    CHECK(std::abs(result.clanImpact->gain - (WeightedClanPp({{"123",600},{"1",500},{"2",450},{"3",400}}) - input.clan.pp)) < 1e-8);
    input.chart.predictedPp = 300;
    CHECK(!EvaluateClanImpact(input, "123", .5, result));
    input.chart.predictedPp = 299;
    CHECK(!EvaluateClanImpact(input, "123", .5, result));
    // Eleven contributors: an unchanged position gives only 0.8^10 of the raw improvement.
    input.clan.scores.clear();
    for (int i = 0; i < 10; ++i) input.clan.scores.push_back({std::to_string(i), 1000.0 - i});
    input.clan.scores.push_back({"123", 400}); input.clan.pp = WeightedClanPp(input.clan.scores);
    input.rivalPp = input.clan.pp + 100; input.chart.predictedPp = 450;
    CHECK(EvaluateClanImpact(input, "123", .5, result));
    CHECK(std::abs(result.clanImpact->gain - 50 * std::pow(.8, 10)) < 1e-8);
    input.chart.predictedPp = 401;
    CHECK(!EvaluateClanImpact(input, "123", .5, result)); // Less than one weighted PP.
    input.rivalPp = input.clan.pp; // A tiny improvement can break a contested tie.
    CHECK(EvaluateClanImpact(input, "123", .5, result) && result.clanImpact->captures);
    input.held = true; CHECK(!EvaluateClanImpact(input, "123", .5, result)); input.held = false;
    input.chart.clearChance = .4;
    CHECK(!EvaluateClanImpact(input, "123", .5, result));
    CHECK(EvaluateClanImpact(input, "123", 0, result) && result.clanImpact->gapClosure == 1);

    // A modifier PB remains the full replacement baseline, even when normal PP is lower.
    input = {}; input.chart = ClanProjection(2, 300); input.chart.currentPp = 350; input.rivalPp = 1000;
    input.clan.scores = {{"123",350,.98,10}}; input.clan.pp = 350;
    CHECK(!EvaluateClanImpact(input, "123", .5, result));
    // Hypothetical switching transfers the existing PB before estimating gameplay gain.
    input = {}; input.chart = ClanProjection(3, 200); input.hypothetical = true;
    input.clan.scores = {{"2",300}}; input.clan.pp = 300;
    input.primaryClan.scores = {{"1",600},{"123",100}}; input.primaryClan.pp = 680;
    input.rivalPp = 500;
    CHECK(EvaluateClanImpact(input, "123", .5, result));
    CHECK(result.clanImpact->hypothetical && std::abs(result.clanImpact->currentClanPp - 380) < 1e-8);
    CHECK(std::abs(result.clanImpact->rivalPp - 600) < 1e-8 && std::abs(result.clanImpact->gain - 80) < 1e-8);
    input.primaryClan.scores = {{"123",100}}; input.primaryClan.pp = 100; input.rivalPp = 350;
    CHECK(!EvaluateClanImpact(input, "123", .5, result)); // Switching already captures it.

    // Capture tiers precede progress; gap fraction precedes absolute contribution.
    std::vector<MapEntry> entries;
    for (int i = 0; i < 3; ++i) {
        input = {}; input.chart = ClanProjection(10+i, i == 0 ? 100 : i == 1 ? 500 : 80);
        input.rivalPp = i == 0 ? 90 : i == 1 ? 2000 : 100;
        CHECK(EvaluateClanImpact(input, "123", .5, result)); entries.push_back(result);
    }
    auto sorted = SelectClanRecommendations(entries, 16);
    CHECK(sorted.size() == 3 && sorted[0].hash == Hash(10) && sorted[1].hash == Hash(12));
    auto duplicate = sorted[0]; duplicate.leaderboardId = "other-chart"; duplicate.clanImpact->adjustedGain += 1;
    entries.push_back(duplicate);
    sorted = SelectClanRecommendations(entries, 1);
    CHECK(sorted.size() == 1 && sorted[0].leaderboardId == "other-chart");

    for (bool v4 : {false, true}) {
        input = {}; input.chart = ClanProjection(40, 1);
        if (v4) { input.chart.difficulties[0].noteSpan = 120; input.chart.difficulties[0].notes = 1000; input.chart.difficulties[0].predictedAcc = .97; }
        const auto& d = input.chart.difficulties[0];
        input.chart.predictedPp = PredictBeatLeaderPp(.95, d);
        input.rivalPp = PredictBeatLeaderPp(.90, d);
        CHECK(EvaluateClanImpact(input, "123", .5, result));
        CHECK(result.clanImpact->captures && result.clanImpact->requiredAccuracy.has_value());
        CHECK(*result.clanImpact->requiredAccuracy > .90 && *result.clanImpact->requiredAccuracy < .95);
        CHECK(std::abs(*result.clanImpact->requiredPp - input.rivalPp - .01) < .001);
        input.rivalPp = PredictBeatLeaderPp(1, d) + 100;
        CHECK(EvaluateClanImpact(input, "123", .5, result) && !result.clanImpact->requiredAccuracy);
    }
    auto history = ClanTestHistory();
    auto charts = EvaluatePlayerPerformance({Score(500)}, history.scores);
    CHECK(charts.size() == 1 && charts[0].predictedPp > 0);
    CHECK(EvaluatePlayerPerformance({Score(500)}, {}).empty());
    auto pb = Score(500, 9999); history.scores.push_back(pb);
    charts = EvaluatePlayerPerformance({Score(500)}, history.scores);
    CHECK(charts[0].currentPp == 9999); // No arbitrary replay uplift.
    CHECK(ClanVerificationBudget(1) == 64 && ClanVerificationBudget(16) == 64 && ClanVerificationBudget(100) == 256);
    std::vector<ClanScreenCandidate> pool;
    for (int i = 0; i < 400; ++i) pool.push_back({ClanProjection(1000+i,100), 1.0/(i+1),100, i<100});
    std::size_t cursor = 0;
    const auto first = ShortlistClanCandidates(pool,64,cursor), second = ShortlistClanCandidates(pool,64,cursor);
    CHECK(first.size() == 64 && second.size() == 64 && cursor == 32);
    std::set<std::string> one, two;
    for (const auto& c : first) CHECK(one.insert(c.chart.leaderboardId).second);
    for (const auto& c : second) two.insert(c.chart.leaderboardId);
    CHECK(one != two);
    CHECK(first[0].chart.leaderboardId == second[0].chart.leaderboardId);
    std::cout << "PASS: personalized weighted clan impact, replay baselines, transfers, targets and search budgets\n";
}

std::string ClanApiChart(int id, double stars = 7) {
    auto json = Chart(id, stars);
    json.insert(1, "\"leaderboardId\":\"" + std::to_string(id) + "\","); return json;
}
std::string ClanStateJson(const std::string& lb, int clan, const std::vector<ClanScore>& scores,
                         int total = -1, double pp = -1) {
    std::ostringstream json; json << std::setprecision(12);
    json << "{\"leaderboardId\":\"" << lb << "\",\"clan\":{\"id\":" << clan << "},\"pp\":"
         << (pp < 0 ? WeightedClanPp(scores) : pp) << ",\"associatedScoresCount\":" << (total < 0 ? scores.size() : total) << ",\"associatedScores\":[";
    for (std::size_t i = 0; i < scores.size(); ++i) {
        const auto& s = scores[i]; if (i) json << ',';
        json << "{\"playerId\":\"" << s.playerId << "\",\"pp\":" << s.pp << ",\"accuracy\":" << s.accuracy << ",\"timepost\":" << s.timepost << '}';
    }
    return json.str() + "]}";
}
void ClanSearchContract() {
    auto history = ClanTestHistory();
    std::string error;
    std::vector<ClanPlaylistRecommendations> lists;
    int catalogs = 0, globals = 0, verifications = 0;
    HttpGetter get = [&](const std::string& url, std::string& body, std::string&) {
        if (url.find("/player/") != std::string::npos) body = R"({"clans":[{"id":1,"tag":"A"},{"id":2,"tag":"B"}],"clanOrder":"A,B"})";
        else if (url.find("/maps?type=ranked") != std::string::npos) {
            ++catalogs; body = url.ends_with("page=1") ? Page(1,2,{ClanApiChart(1)}) : Page(2,2,{ClanApiChart(2)});
        } else if (url.find("/clans/globalmap") != std::string::npos) { ++globals; body = R"({"points":[]})"; }
        else if (url.find("/clan/") != std::string::npos && url.find("/maps?") != std::string::npos) body = Page(1,0,{});
        else if (url.find("/clan/") != std::string::npos) {
            ++verifications;
            const std::string lb = url.find("/clanRankings/1/") != std::string::npos ? "1" : "2";
            body = ClanStateJson(lb, url.find("/clan/1?") != std::string::npos ? 1 : 2, {});
        } else {
            const std::string lb = url.find("/clanRankings/1?") != std::string::npos ? "1" : "2";
            body = "{\"id\":\"" + lb + "\",\"clanRanking\":[{\"clan\":{\"id\":3},\"pp\":100}]}";
        }
        return true;
    };
    HttpGetter icons = [](const std::string&, std::string&, std::string&) { return false; };
    CHECK(FetchClanToConquerInternal("123", true,16,lists,error,get,icons,.5,&history,false));
    CHECK(catalogs == 2 && globals == 1 && verifications == 4);
    CHECK(lists.size() == 2 && lists[0].entries.size() == 2 && lists[1].entries.size() == 2);
    CHECK(!lists[0].hypothetical && lists[1].hypothetical && lists[1].entries[0].clanImpact->hypothetical);
    CHECK(lists[0].eligibleCharts == 2 && lists[0].checkedCharts == 2 && lists[0].failedCharts == 0);
    CHECK(lists[0].entries[0].clanImpact->captures); // Unknown global ownership is verified, never assumed zero.
    // Completing a recommendation at the predicted score removes that chart on
    // refresh, rather than treating the full new score as another contribution.
    auto completed = Score(1);
    completed.leaderboardId = "1";
    completed.pp = PredictBeatLeaderPp(.95, completed.difficulties[0]);
    history.scores.push_back(completed);
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,get,icons,.5,&history,false));
    CHECK(lists[0].eligibleCharts == 1 && lists[0].entries.size() == 1);
    CHECK(lists[0].entries[0].hash == Hash(2));
    history.scores.pop_back();
    HttpGetter wideStars = [&](const std::string& url, std::string& body, std::string& failure) {
        if (url.find("/maps?type=ranked") != std::string::npos) {
            body = Page(1,2,{ClanApiChart(1,1),ClanApiChart(2,22)}); return true;
        }
        return get(url,body,failure);
    };
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,wideStars,icons,0,&history,false));
    CHECK(lists[0].eligibleCharts == 2 && lists[0].entries.size() == 2);
    CHECK(std::any_of(lists[0].entries.begin(),lists[0].entries.end(),[](const auto& e) { return e.stars == 1; }));
    CHECK(std::any_of(lists[0].entries.begin(),lists[0].entries.end(),[](const auto& e) { return e.stars == 22; }));
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,wideStars,icons,.5,&history,false));
    CHECK(lists[0].entries.size() == 1 && lists[0].entries[0].stars == 1);
    auto fail = [&](const std::string& url, std::string& body, std::string& failure) {
        if (url.find("/clanRankings/") != std::string::npos) { failure = "HTTP 503"; return false; }
        return get(url,body,failure);
    };
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,fail,icons,.5,&history,false));
    CHECK(!lists[0].error.empty() && lists[0].failedCharts == 2 && !lists[0].cursorReady);
    auto partial = [&](const std::string& url, std::string& body, std::string& failure) {
        if (url.find("/clanRankings/1?") != std::string::npos) return false;
        return get(url,body,failure);
    };
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,partial,icons,.5,&history,false));
    CHECK(lists[0].error.empty() && lists[0].entries.size() == 1 && lists[0].failedCharts == 1 && !lists[0].warning.empty());
    int limitedCalls = 0;
    auto limited = [&](const std::string& url, std::string& body, std::string& failure) {
        if (url.find("/clanRankings/") != std::string::npos) { ++limitedCalls; failure = "Leaderboard API returned HTTP 429."; return false; }
        return get(url,body,failure);
    };
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,limited,icons,.5,&history,false));
    CHECK(limitedCalls == 1 && lists[0].checkedCharts == 1 && lists[0].failedCharts == 1 && !lists[0].error.empty());
    CHECK(lists[0].warning.find("rate limited") != std::string::npos && !lists[0].cursorReady);
    auto essential = [&](const std::string& url, std::string& body, std::string& failure) {
        if (url.find("globalmap") != std::string::npos) { body = "{}"; return true; }
        return get(url,body,failure);
    };
    CHECK(!FetchClanToConquerInternal("123",false,16,lists,error,essential,icons,.5,&history,false));
    auto oneClanFails = [&](const std::string& url, std::string& body, std::string& failure) {
        if (url.find("/clan/A/maps?") != std::string::npos) { body = "{}"; return true; }
        return get(url,body,failure);
    };
    CHECK(FetchClanToConquerInternal("123",true,16,lists,error,oneClanFails,icons,.5,&history,false));
    CHECK(!lists[0].error.empty() && lists[1].error.empty() && lists[1].entries.size() == 2);
    ClanMapState state;
    int pages = 0;
    HttpGetter paginated = [&](const std::string&, std::string& body, std::string&) {
        ++pages; body = ClanStateJson("1",1,{{std::to_string(pages), pages == 1 ? 100.0 : 50.0}},2,140); return true;
    };
    CHECK(FetchClanMapState("1",1,state,error,paginated,false) && pages == 2 && state.scores.size() == 2);
    pages = 0;
    HttpGetter incomplete = [&](const std::string&, std::string& body, std::string&) {
        body = ClanStateJson("1",1,++pages == 1 ? std::vector<ClanScore>{{"1",100}} : std::vector<ClanScore>{},2,140); return true;
    };
    CHECK(!FetchClanMapState("1",1,state,error,incomplete,false));
    HttpGetter mismatch = [&](const std::string&, std::string& body, std::string&) {
        body = ClanStateJson("1",1,{{"1",100}},1,200); return true;
    };
    CHECK(!FetchClanMapState("1",1,state,error,mismatch,false));
    HttpGetter invalidNumber = [](const std::string&, std::string& body, std::string&) {
        body = R"({"leaderboardId":"1","clan":{"id":1},"pp":"nan","associatedScoresCount":0,"associatedScores":[]})"; return true;
    };
    CHECK(!FetchClanMapState("1",1,state,error,invalidNumber,false));
    HttpGetter missing = [](const std::string&, std::string& body, std::string&) { body = "null"; return true; };
    CHECK(FetchClanMapState("1",1,state,error,missing,false) && state.pp == 0 && state.scores.empty());
    int detailCalls = 0;
    HttpGetter budgeted = [&](const std::string& url, std::string& body, std::string& failure) {
        if (url.find("/maps?type=ranked") != std::string::npos) {
            std::vector<std::string> charts; for (int i = 1300; i < 1430; ++i) charts.push_back(ClanApiChart(i));
            body = Page(1,130,charts); return true;
        }
        const std::string token = "/clanRankings/";
        if (const auto p = url.find(token); p != std::string::npos) {
            const auto first = p + token.size(), end = url.find_first_of("/?",first);
            const auto lb = url.substr(first,end-first);
            if (url.find("/clan/") != std::string::npos) { ++detailCalls; body = ClanStateJson(lb,1,{}); }
            else body = "{\"id\":\"" + lb + "\",\"clanRanking\":[{\"clan\":{\"id\":3},\"pp\":100}]}";
            return true;
        }
        return get(url,body,failure);
    };
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,budgeted,icons,.5,&history,false));
    CHECK(lists[0].eligibleCharts == 130 && lists[0].checkedCharts == 64 && detailCalls == 64);
    CHECK(lists[0].entries.size() == 16 && !lists[0].warning.empty());
    CHECK(PersistClanCursor("123","A",47,error));
    std::string warning; CHECK(ReadClanCursor("123","A",warning) == 47 && warning.empty());
    std::ofstream(ClanCursorPath("123","A")) << "broken";
    CHECK(ReadClanCursor("123","A",warning) == 0 && !warning.empty());
    std::filesystem::remove(ClanCursorPath("123","A"));
    std::filesystem::remove(std::filesystem::path(RANKEDTOOLS_CLAN_CURSOR_DIRECTORY));
    std::cout << "PASS: full clan search, untouched charts, partial errors, independent clans, pagination and cursors\n";
}
