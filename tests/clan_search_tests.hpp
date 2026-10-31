// Search quality and stability, using the production API path with fixed inputs.
void ClanSearchLearning() {
    auto chart=ClanProjection(40,400);
    ClanSearchEvidence e{chart.leaderboardId,2000,2001,0,400,.95,2000,2001,.001,false};
    CHECK(ScreenClanCandidate(chart,2000,2001,.5).optimisticCapture);
    CHECK(!ScreenClanCandidate(chart,2000,2001,.5,&e).optimisticCapture);
    CHECK(ScreenClanCandidate(chart,2000,2001,.5,&e).optimisticGain==0);
    e.gain=100; e.useful=true; e.rivalPp=2050;
    CHECK(ScreenClanCandidate(chart,2000,2001,.5,&e).optimisticCapture);
    CHECK(std::abs(ScreenClanCandidate(chart,2000,2001,.5,&e).optimisticGain-90)<1e-8);
    CHECK(ScreenClanCandidate(chart,2000,2001,0,&e).optimisticGain==100);
    CHECK(ClanEvidenceMatches(e,chart,2000,2001));
    CHECK(!ClanEvidenceMatches(e,chart,2001,2001));
    CHECK(!ClanEvidenceMatches(e,chart,2000,2002));
    auto changed=chart; changed.currentPp=1;
    CHECK(!ClanEvidenceMatches(e,changed,2000,2001));
    changed=chart; changed.predictedPp+=1;
    CHECK(!ClanEvidenceMatches(e,changed,2000,2001));
    changed=chart; changed.predictedAccuracy+=.001;
    CHECK(!ClanEvidenceMatches(e,changed,2000,2001));
    // Changed skill/PB/bulk data returns the chart to optimistic screening.
    CHECK(ScreenClanCandidate(chart,2100,2101,.5,&e).optimisticGain==360);

    std::string error,warning;
    const std::string tag="LEARNING_TEST";
    const auto path=ClanCursorPath("123",tag);
    ClanSearchState state; state.cursor=47; state.primaryClanId=1;
    state.recommendations={chart.leaderboardId}; state.evidence={e};
    CHECK(PersistClanSearchState("123",tag,state,error));
    auto restored=ReadClanSearchState("123",tag,warning);
    CHECK(warning.empty() && restored.cursor==47 && restored.primaryClanId==1);
    CHECK(restored.recommendations==state.recommendations && restored.evidence.size()==1);
    CHECK(restored.evidence[0].gain==100 && restored.evidence[0].useful);
    auto invalid=state; invalid.evidence[0].gain=std::numeric_limits<double>::quiet_NaN();
    CHECK(!PersistClanSearchState("123",tag,invalid,error));
    restored=ReadClanSearchState("123",tag,warning);
    CHECK(restored.evidence.size()==1 && restored.evidence[0].gain==100); // Keep the good file.
    { std::ofstream file(path); file << "{\"version\":1,\"cursor\":23}"; }
    restored=ReadClanSearchState("123",tag,warning);
    CHECK(restored.cursor==23 && restored.recommendations.empty() && warning.empty());
    CHECK(PersistClanSearchState("123",tag,state,error));
    { std::ifstream file(path); std::string json{std::istreambuf_iterator<char>(file),{}};
      const auto epoch=json.find("\"epoch\":"); json.replace(epoch+8,1,"0");
      std::ofstream out(path); out << json; }
    restored=ReadClanSearchState("123",tag,warning);
    CHECK(restored.evidence.empty() && !warning.empty());
    std::filesystem::remove(path);
    std::cout << "PASS: learned weighted impact screening, input invalidation and restart persistence\n";
}

void ConsecutiveClanRefreshes(bool betterDiscoveries=false) {
    const std::string player="123",tag="STABILITY_TEST";
    const auto path=ClanCursorPath(player,tag);
    std::filesystem::remove(path);
    auto history=ClanTestHistory();
    std::vector<ClanScore> crowded;
    for (int i=0;i<60;++i) crowded.push_back({std::to_string(90000+i),700,.98,100});
    const double total=WeightedClanPp(crowded);
    const std::vector<ClanScore> weakerContributions={{"80000",700,.98,100}};
    std::string contested,failed;
    std::set<std::string> requested;
    int crowdedChecks=0;
    HttpGetter get=[&](const std::string& url,std::string& body,std::string& error) {
        if (url.find("/player/")!=std::string::npos) body="{\"clans\":[{\"id\":1,\"tag\":\""+tag+"\"}]}";
        else if (url.find("/maps?type=ranked")!=std::string::npos) {
            std::vector<std::string> rows;
            for(int i=1300;i<1430;++i) rows.push_back(ClanApiChart(i));
            body=Page(1,130,rows);
        } else if (url.find("/clans/globalmap")!=std::string::npos) {
            std::ostringstream json; json << std::setprecision(14) << "{\"points\":[";
            for(int i=1300;i<1430;++i) {
                if(i>1300) json << ',';
                const double rival=std::to_string(i)==contested?100000:i<1348?total+1:betterDiscoveries && i<1364?800:100;
                json << "{\"leaderboardId\":\"" << i << "\",\"clans\":[{\"id\":3,\"pp\":" << rival << "}]}";
            }
            body=json.str()+"]}";
        } else if (url.find("/maps?")!=std::string::npos) {
            std::vector<std::string> rows;
            for(int i=1300;i<(betterDiscoveries?1364:1348);++i) {
                std::ostringstream json; json << std::setprecision(14) << "{\"leaderboardId\":\"" << i << "\",\"pp\":" << (i<1348?total:700) << ",\"rank\":2}";
                rows.push_back(json.str());
            }
            body=Page(1,rows.size(),rows);
        } else {
            const auto start=url.find("/clanRankings/")+14,end=url.find_first_of("/?",start);
            const auto lb=url.substr(start,end-start);
            const bool dense=std::stoi(lb)<1348;
            const bool weaker=betterDiscoveries && !dense && std::stoi(lb)<1364;
            if (url.find("/clan/")!=std::string::npos) body=ClanStateJson(lb,1,dense?crowded:weaker?weakerContributions:std::vector<ClanScore>{});
            else {
                requested.insert(lb); if(dense) ++crowdedChecks;
                if(lb==failed) { error="HTTP 503"; return false; }
                const double rival=lb==contested?100000:dense?total+1:weaker?800:100;
                body=FreshClanRanking(std::stoi(lb),7,rival);
                if(dense || weaker) {
                    const auto p=body.find("]}"); std::ostringstream extra;
                    extra << std::setprecision(14) << ",{\"clan\":{\"id\":1},\"pp\":" << (dense?total:700) << '}';
                    body.insert(p,extra.str());
                }
            }
        }
        return true;
    };
    HttpGetter icons=[](const std::string&,std::string&,std::string&){return false;};
    std::string error;
    std::vector<ClanPlaylistRecommendations> lists;
    ClanScreenCache cache(false);
    std::vector<std::string> original;
    double originalGain=0;
    std::size_t previousCursor=0;
    for(int pass=0;pass<4;++pass) {
        crowdedChecks=0; requested.clear();
        CHECK(FetchClanToConquerInternal(player,false,16,lists,error,get,icons,.5,&history,false,&cache));
        CHECK(lists.size()==1 && lists[0].error.empty() && lists[0].failedCharts==0);
        const auto& list=lists[0];
        CHECK(list.entries.size()==16 && list.checkedCharts==64 && requested.size()==64);
        std::vector<std::string> ids;
        for(const auto& entry:list.entries) { CHECK(entry.clanImpact->captures); ids.push_back(entry.leaderboardId); }
        if(pass==0) { original=ids; originalGain=list.entries[0].clanImpact->gain; CHECK(crowdedChecks==48 && list.recheckedCharts==0); }
        else {
            if(betterDiscoveries && pass==1) {
                // No API/skill changes: genuinely greater clan benefit wins as it is discovered.
                CHECK(ids!=original);
                for(const auto& entry:list.entries) CHECK(entry.clanImpact->gain>originalGain+1);
                original=ids;
            } else CHECK(ids==original); // Previously reproduced 0/16 retained with identical inputs.
            CHECK(list.recheckedCharts==16 && list.learnedCharts>=64);
            CHECK(crowdedChecks<=12); // Spend the new-opportunity quota on useful alternatives.
        }
        CHECK(list.nextCursor!=previousCursor); previousCursor=list.nextCursor;
        CHECK(PersistClanSearchState(player,tag,list.searchState,error));
    }
    // A real rival change displaces a prior capture; remembered picks aren't pinned.
    contested=original[0];
    CHECK(FetchClanToConquerInternal(player,false,16,lists,error,get,icons,.5,&history,false,&cache));
    CHECK(lists[0].recheckedCharts==16);
    CHECK(std::none_of(lists[0].entries.begin(),lists[0].entries.end(),[&](const auto& e){return e.leaderboardId==contested;}));
    CHECK(PersistClanSearchState(player,tag,lists[0].searchState,error));
    // Playing above the predicted target removes that replay from the search.
    auto pb=Score(std::stoi(original[1]),9999); pb.leaderboardId=original[1]; history.scores.push_back(pb);
    requested.clear();
    CHECK(FetchClanToConquerInternal(player,false,16,lists,error,get,icons,.5,&history,false,&cache));
    CHECK(std::none_of(lists[0].entries.begin(),lists[0].entries.end(),[&](const auto& e){return e.leaderboardId==pb.leaderboardId;}));
    CHECK(!requested.contains(pb.leaderboardId));
    CHECK(PersistClanSearchState(player,tag,lists[0].searchState,error));
    failed=lists[0].entries[0].leaderboardId;
    CHECK(FetchClanToConquerInternal(player,false,16,lists,error,get,icons,.5,&history,false,&cache));
    CHECK(lists[0].failedCharts==1 && lists[0].error.empty());
    CHECK(std::none_of(lists[0].entries.begin(),lists[0].entries.end(),[&](const auto& e){return e.leaderboardId==failed;}));
    CHECK(std::find(lists[0].searchState.recommendations.begin(),lists[0].searchState.recommendations.end(),failed)!=lists[0].searchState.recommendations.end());
    CHECK(PersistClanSearchState(player,tag,lists[0].searchState,error));
    const auto retry=failed; failed.clear(); requested.clear();
    CHECK(FetchClanToConquerInternal(player,false,16,lists,error,get,icons,.5,&history,false,&cache));
    CHECK(requested.contains(retry) && lists[0].failedCharts==0);
    auto otherPrimary=lists[0].searchState; otherPrimary.primaryClanId=2;
    CHECK(PersistClanSearchState(player,tag,otherPrimary,error));
    CHECK(FetchClanToConquerInternal(player,false,16,lists,error,get,icons,.5,&history,false,&cache));
    CHECK(lists[0].recheckedCharts==0 && lists[0].learnedCharts==0 && lists[0].searchState.primaryClanId==1);
    std::filesystem::remove(path);
    std::filesystem::remove(std::filesystem::path(RANKEDTOOLS_CLAN_CURSOR_DIRECTORY));
    std::cout << (betterDiscoveries?"PASS: better discovered clan contributions replace previous picks without score changes\n":
        "PASS: consecutive clan refresh stability, learned search quality, live displacement, completed plays and failed-pick retry\n");
}

void LearnedSecondaryClans() {
    const std::string a="LEARN_PRIMARY",b="LEARN_SECONDARY";
    std::filesystem::remove(ClanCursorPath("123",a)); std::filesystem::remove(ClanCursorPath("123",b));
    auto history=ClanTestHistory();
    auto existing=Score(1,100); existing.leaderboardId="1"; history.scores.push_back(existing);
    double pb=100;
    HttpGetter get=[&](const std::string& url,std::string& body,std::string&) {
        if(url.find("/player/")!=std::string::npos)
            body="{\"clans\":[{\"id\":1,\"tag\":\""+a+"\"},{\"id\":2,\"tag\":\""+b+"\"}],\"clanOrder\":\""+a+","+b+"\"}";
        else if(url.find("/maps?type=ranked")!=std::string::npos) body=Page(1,1,{ClanApiChart(1)});
        else if(url.find("globalmap")!=std::string::npos) body=R"({"points":[]})";
        else if(url.find("/maps?")!=std::string::npos) body=Page(1,0,{});
        else if(url.find("/clan/1?")!=std::string::npos) body=ClanStateJson("1",1,{{"123",pb,.95,10}});
        else if(url.find("/clan/2?")!=std::string::npos) body=ClanStateJson("1",2,{});
        else {
            const std::string primary="{\"clan\":{\"id\":1},\"pp\":"+std::to_string(pb)+'}';
            const std::string rival=R"({"clan":{"id":3},"pp":200})";
            body=FreshClanRanking(1);
            const auto start=body.find("\"clanRanking\":");
            body.replace(start,std::string::npos,"\"clanRanking\":["+(pb>200?primary+','+rival:rival+','+primary)+"]}");
        }
        return true;
    };
    HttpGetter icon=[](const std::string&,std::string&,std::string&){return false;};
    std::string error;
    std::vector<ClanPlaylistRecommendations> lists;
    for(int pass=0;pass<2;++pass) {
        CHECK(FetchClanToConquerInternal("123",true,16,lists,error,get,icon,.5,&history,false));
        CHECK(lists.size()==2 && lists[0].entries.size()==1 && lists[1].entries.size()==1);
        for(const auto& list:lists) {
            CHECK(list.error.empty() && list.recheckedCharts==static_cast<std::size_t>(pass));
            CHECK(list.learnedCharts==static_cast<std::size_t>(pass));
            CHECK(std::abs(list.entries[0].clanImpact->currentClanPp-100)<1e-8);
            CHECK(std::abs(list.entries[0].clanImpact->gain-(list.entries[0].predictedPp-100))<1e-8);
            CHECK(PersistClanSearchState("123",list.clanTag,list.searchState,error));
        }
        CHECK(!lists[0].entries[0].clanImpact->hypothetical && lists[1].entries[0].clanImpact->hypothetical);
    }
    pb=2000; // Switching alone now captures the secondary map; playing is not a new benefit.
    CHECK(FetchClanToConquerInternal("123",true,16,lists,error,get,icon,.5,&history,false));
    CHECK(lists[0].entries.empty() && lists[1].entries.empty());
    CHECK(lists[0].failedCharts==0 && lists[1].failedCharts==0);
    std::filesystem::remove(ClanCursorPath("123",a)); std::filesystem::remove(ClanCursorPath("123",b));
    std::filesystem::remove(std::filesystem::path(RANKEDTOOLS_CLAN_CURSOR_DIRECTORY));
    std::cout << "PASS: independent learned clan searches, secondary PB transfer and fresh capture exclusion\n";
}
