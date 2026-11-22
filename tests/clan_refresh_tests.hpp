std::string FreshClanRanking(int id, double stars = 7, double rival = 100, bool v4 = false,
                             std::string_view modifierFields = {}) {
    std::ostringstream out; out << std::setprecision(15);
    out << "{\"id\":\"" << id << "\",\"song\":{\"hash\":\"" << Hash(id) << "\",\"name\":\"Test\"},"
        << "\"difficulty\":{\"difficultyName\":\"ExpertPlus\",\"modeName\":\"Standard\",\"status\":3,\"stars\":" << stars
        << ",\"accRating\":8,\"passRating\":8,\"techRating\":5";
    if (v4) out << ",\"noteSpan\":120,\"notes\":1000,\"predictedAcc\":0.97,\"sliderPercentage\":0.1";
    out << modifierFields;
    out << "},\"clanRanking\":[{\"clan\":{\"id\":3},\"pp\":" << rival << "}]}";
    return out.str();
}

void ClanModifierRefresh() {
    constexpr int id = 45;
    const std::string modifiers = R"(,"modifierValues":{"fs":0.4,"da":0.08},"modifiersRating":{
        "fsStars":11,"fsAccRating":14,"fsPassRating":10,"fsTechRating":6,"fsPredictedAcc":0.97,"fsSliderPercentage":0.2})";
    auto catalog = ClanApiChart(id);
    catalog.insert(catalog.size() - 1,R"(,"noteSpan":120,"notes":1000,"predictedAcc":0.97,"sliderPercentage":0.1)");
    catalog.insert(catalog.size() - 1, modifiers);
    auto chart = Parse(catalog).front();
    auto history = ClanTestHistory();
    const auto options = EvaluatePlayerPerformance({chart},history.scores,{},true);
    CHECK(options.size() == 2);
    auto pb = Score(id);
    pb.pp = std::ceil(options.front().predictedPp) + 1;
    history.scores.push_back(pb);
    CHECK(options.back().predictedPp > pb.pp + 1);
    const auto live = FreshClanRanking(id, 7, (pb.pp + options.back().predictedPp) / 2, true, modifiers);

    int catalogRequests = 0, rankingRequests = 0;
    const HttpGetter get = [&](const std::string& url, std::string& json, std::string&) {
        if (url.find("/player/") != std::string::npos) json = R"({"clans":[{"id":1,"tag":"A"}]})";
        else if (url.find("/maps?type=ranked") != std::string::npos) { ++catalogRequests; json = Page(1,1,{catalog}); }
        else if (url.find("/clans/globalmap") != std::string::npos) json = R"({"points":[]})";
        else if (url.find("sortBy=toconquer") != std::string::npos)
            json = Page(1,1,{R"({"leaderboardId":"45","pp":-10,"leaderboard":)" + live + "}"});
        else if (url.find("/maps?") != std::string::npos) json = Page(1,0,{});
        else if (url.find("/clan/1?") != std::string::npos)
            json = history.scores.size() == 2 ? ClanStateJson("45",1,{{"123",pb.pp,.95,10}}) : "null";
        else { ++rankingRequests; json = live; }
        return true;
    };
    const HttpGetter icon = [](const std::string&,std::string&,std::string&) { return false; };
    ClanScreenCache cache(false);
    std::vector<ClanPlaylistRecommendations> lists;
    std::string error;
    const auto refresh = [&](double clearRate, bool legacy, bool enabled) -> const ClanPlaylistRecommendations& {
        CHECK(FetchClanToConquerInternal("123",false,16,lists,error,get,icon,clearRate,&history,false,&cache,legacy,enabled));
        CHECK(lists.size() == 1 && lists.front().error.empty());
        return lists.front();
    };

    CHECK(refresh(0,false,false).entries.empty());
    CHECK(refresh(.5,false,true).entries.empty()); // The base-speed clear does not qualify the faster variant.
    CHECK(rankingRequests == 0);
    const auto& personalized = refresh(0,false,true);
    CHECK(catalogRequests == 1 && rankingRequests == 1); // Modifier ratings survive the catalog cache.
    CHECK(personalized.eligibleCharts == 1 && personalized.entries.size() == 1);
    const auto& entry = personalized.entries.front();
    CHECK(entry.recommendedModifier == "Faster Song (1.20×) + Disappearing Arrows");
    CHECK(entry.stars == 11 && entry.clearChance < .5);
    CHECK(entry.currentPp == pb.pp && entry.isImprovement);
    CHECK(entry.difficulties.front().noteSpan == 100 && entry.difficulties.front().sliderPercentage == .2);
    CHECK(std::abs(entry.clanImpact->gain - (entry.predictedPp - pb.pp)) < 1e-8);
    CHECK(entry.clanImpact->requiredAccuracy && entry.clanImpact->requiredPp);
    CHECK(std::abs(*entry.clanImpact->requiredPp - PredictBeatLeaderPp(
        *entry.clanImpact->requiredAccuracy,entry.difficulties.front(),1.08)) < 1e-8);

    CHECK(refresh(0,true,false).entries.empty());
    CHECK(refresh(.5,true,true).entries.empty());
    const auto& legacy = refresh(0,true,true);
    CHECK(legacy.legacy && legacy.eligibleCharts == 1 && legacy.entries.size() == 1);
    CHECK(legacy.entries.front().recommendedModifier == "Faster Song (1.20×) + Disappearing Arrows");
    CHECK(legacy.entries.front().priority == -10);

    // Both variants may compete on a new chart, while verification stays per leaderboard.
    history.scores.pop_back();
    const auto& unplayed = refresh(0,false,true);
    CHECK(unplayed.eligibleCharts == 1 && unplayed.checkedCharts == 1 && unplayed.entries.size() == 1);
    CHECK(!unplayed.entries.front().isImprovement && unplayed.entries.front().clanImpact->captures);
    CHECK(unplayed.entries.front().recommendedModifier == "Faster Song (1.20×) + Disappearing Arrows");
    std::cout << "PASS: clan modifier toggle, cached ratings, clear-rate gating, modified capture target and legacy cap\n";
}
void ClanRefreshPerformance() {
    const double now = ClanCacheNow();
    std::string body;
    ClanScreenCache memory(false);
    CHECK(memory.Put("test","value",now));
    CHECK(memory.Get("test",10,now+9,body) && body == "value");
    CHECK(!memory.Get("test",10,now+10,body));
    CHECK(!memory.Get("test",10,now-1,body));
    CHECK(!memory.Put("empty","",now));
    ClanScreenCache disk;
    CHECK(disk.Put("test", "payload",now));
    ClanScreenCache reopened;
    CHECK(reopened.Get("test",10,now+1,body) && body == "payload");
    { std::ofstream file(ClanScreenCache::Path("test")); file << "broken"; }
    ClanScreenCache damaged;
    CHECK(!damaged.Get("test",10,now+1,body));
    { std::ofstream file(ClanScreenCache::Path("test")); file << "{\"version\":1,\"epoch\":4,\"savedAt\":" << std::setprecision(16) << now << ",\"body\":\"payload\"}"; }
    ClanScreenCache obsolete;
    CHECK(!obsolete.Get("test",10,now+1,body));
    std::filesystem::remove(ClanScreenCache::Path("test"));
    std::filesystem::remove(std::filesystem::path(RANKEDTOOLS_CLAN_CURSOR_DIRECTORY));

    auto legacy = Score(10); legacy.leaderboardId = "10";
    auto modern = Score(11); modern.leaderboardId = "11"; modern.difficulties[0].noteSpan = 0;
    modern.difficulties[0].notes = 1000; modern.difficulties[0].predictedAcc = .97; modern.difficulties[0].sliderPercentage = .2;
    auto restored = Parse(CompactClanCatalog({legacy,modern}));
    CHECK(restored.size() == 2 && !restored[0].difficulties[0].noteSpan && restored[1].difficulties[0].noteSpan == 0);
    CHECK(restored[1].leaderboardId == "11" && restored[1].difficulties[0].notes == 1000);
    CHECK(std::abs(PredictBeatLeaderPp(.95,modern.difficulties[0])-PredictBeatLeaderPp(.95,restored[1].difficulties[0])) < 1e-8);

    int catalogs = 0, totals = 0, ranks = 0, contributions = 0;
    double stars = 7, rival = 100;
    bool v4 = false, rejectRatings = false;
    HttpGetter get = [&](const std::string& url, std::string& json, std::string&) {
        if (url.find("/player/") != std::string::npos) json = R"({"clans":[{"id":1,"tag":"A"}]})";
        else if (url.find("/maps?type=ranked") != std::string::npos) {
            ++catalogs; CHECK(url.find("count=500") != std::string::npos); json = Page(1,1,{ClanApiChart(1)});
        } else if (url.find("globalmap") != std::string::npos) json = R"({"points":[]})";
        else if (url.find("/maps?") != std::string::npos) {
            ++totals; CHECK(url.find("count=500") != std::string::npos); json = Page(1,0,{});
        } else if (url.find("/clan/1?") != std::string::npos) { ++contributions; json = ClanStateJson("1",1,{}); }
        else { ++ranks; json = rejectRatings ? R"({"id":"1","clanRanking":[]})" : FreshClanRanking(1,stars,rival,v4); }
        return true;
    };
    HttpGetter icon = [](const std::string&,std::string&,std::string&) { return false; };
    ClanScreenCache cache(false);
    auto history = ClanTestHistory();
    std::vector<ClanPlaylistRecommendations> lists;
    std::string error;
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,get,icon,.5,&history,false,&cache));
    CHECK(lists[0].entries.size() == 1 && lists[0].entries[0].clanImpact->captures);
    const double originalPp = lists[0].entries[0].predictedPp;
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,get,icon,.5,&history,false,&cache));
    CHECK(catalogs == 1 && totals == 1 && ranks == 2 && contributions == 2);
    CHECK(lists[0].entries[0].predictedPp == originalPp);
    rival = 100000;
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,get,icon,.5,&history,false,&cache));
    CHECK(!lists[0].entries[0].clanImpact->captures && ranks == 3 && contributions == 3);
    // Current ratings and PP formula come from live verification, not the old catalog.
    stars = 1; v4 = true; rival = 1;
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,get,icon,.5,&history,false,&cache));
    CHECK(lists[0].entries[0].stars == 1 && lists[0].entries[0].difficulties[0].noteSpan == 120);
    CHECK(lists[0].entries[0].predictedPp != originalPp && catalogs == 1);
    cache.entries["totals_1"].savedAt -= kClanTotalsCacheSeconds+1;
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,get,icon,.5,&history,false,&cache));
    CHECK(totals == 2 && catalogs == 1);
    cache.entries[kClanCatalogCacheKey].savedAt -= kClanCatalogCacheSeconds+1;
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,get,icon,.5,&history,false,&cache));
    CHECK(catalogs == 2);
    cache.Put(kClanCatalogCacheKey,"{}",ClanCacheNow());
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,get,icon,.5,&history,false,&cache));
    CHECK(catalogs == 3);
    auto pb = Score(1,9999); history.scores.push_back(pb);
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,get,icon,.5,&history,false,&cache));
    CHECK(lists[0].entries.empty() && lists[0].checkedCharts == 0);
    history.scores.pop_back(); rejectRatings = true;
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,get,icon,.5,&history,false,&cache));
    CHECK(lists[0].entries.empty() && lists[0].failedCharts == 1 && !lists[0].error.empty());
    ClanScreenCache limitedCache(false);
    limitedCache.Put(kClanCatalogCacheKey,CompactClanCatalog(Parse(ClanApiChart(1))),ClanCacheNow());
    limitedCache.Put("totals_1",CompactClanTotals({},"A",""),ClanCacheNow());
    limitedCache.Put("totals_2",CompactClanTotals({},"B",""),ClanCacheNow());
    int limitedChecks = 0;
    HttpGetter limited = [&](const std::string& url,std::string& json,std::string& failure) {
        if (url.find("/player/") != std::string::npos) { json = R"({"clans":[{"id":1,"tag":"A"},{"id":2,"tag":"B"}]})"; return true; }
        if (url.find("globalmap") != std::string::npos) { json = R"({"points":[]})"; return true; }
        ++limitedChecks; failure = "HTTP 429"; return false;
    };
    CHECK(FetchClanToConquerInternal("123",true,16,lists,error,limited,icon,.5,&history,false,&limitedCache));
    CHECK(limitedChecks == 1 && lists.size() == 2 && !lists[0].error.empty() && !lists[1].error.empty());
    CHECK(lists[1].checkedCharts == 0 && !lists[1].cursorReady);

    std::atomic<int> active{0}, peak{0}, completed{0};
    RunClanJobs(8,true,[&](std::size_t) {
        const int current = ++active;
        int observed = peak.load(); while(current > observed && !peak.compare_exchange_weak(observed,current)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        --active; ++completed;
    });
    CHECK(completed == 8 && peak == 2 && active == 0);
    bool threw = false;
    try { RunClanJobs(8,true,[&](std::size_t i) { if (i == 0) throw std::runtime_error("worker failure"); ++completed; }); }
    catch(const std::runtime_error&) { threw = true; }
    CHECK(threw && active == 0);
    ClanRequestGate gate;
    std::mutex startsMutex;
    std::vector<std::chrono::steady_clock::time_point> starts;
    RunClanJobs(4,true,[&](std::size_t) {
        if (!gate.Acquire(true)) throw std::runtime_error("Unexpected cancellation");
        std::lock_guard<std::mutex> lock(startsMutex); starts.push_back(std::chrono::steady_clock::now());
    });
    CHECK(starts.size() == 4);
    std::sort(starts.begin(),starts.end());
    for(std::size_t i=1;i<starts.size();++i) CHECK(starts[i]-starts[i-1] >= std::chrono::milliseconds(200));
    gate.limited.store(true); CHECK(!gate.Acquire(false));
    ClanRequestGate pagesGate;
    CHECK(pagesGate.Acquire(true,"https://api.beatleader.com/maps?page=1"));
    const auto firstPage = std::chrono::steady_clock::now();
    CHECK(pagesGate.Acquire(true,"https://api.beatleader.com/maps?page=2"));
    CHECK(std::chrono::steady_clock::now()-firstPage >= std::chrono::milliseconds(200));
    // Production statistics are thread-local; the extra worker's requests must
    // be merged into the parent so the existing refresh log remains accurate.
    HttpGetter parallel = [&](const std::string& url,std::string& json,std::string&) {
        ++httpStatistics.requests; ++httpStatistics.connections; httpStatistics.seconds += .01;
        if (url.find("/player/") != std::string::npos) json = R"({"clans":[{"id":1,"tag":"A"}]})";
        else if (url.find("/maps?type=ranked") != std::string::npos) json = Page(1,2,{ClanApiChart(1),ClanApiChart(2)});
        else if (url.find("globalmap") != std::string::npos) json = R"({"points":[]})";
        else if (url.find("/maps?") != std::string::npos) json = Page(1,0,{});
        else {
            const auto first = url.find("/clanRankings/") + std::string("/clanRankings/").size();
            const int id = std::stoi(url.substr(first,url.find_first_of("/?",first)-first));
            json = url.find("/clan/1?") != std::string::npos ? ClanStateJson(std::to_string(id),1,{}) : FreshClanRanking(id);
        }
        return true;
    };
    history = ClanTestHistory();
    const auto before = GetHttpStatistics();
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,parallel,icon,.5,&history,true));
    const auto after = GetHttpStatistics();
    CHECK(lists[0].entries.size() == 2 && lists[0].failedCharts == 0);
    CHECK(after.requests-before.requests == 8 && after.connections-before.connections == 8);
    CHECK(std::abs(after.seconds-before.seconds-.08) < 1e-9);
    std::cout << "PASS: screening cache TTL/epoch/persistence, live verification, current PBs, two-worker bounds and shared pacing\n";
}
