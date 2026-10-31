std::string LegacyClanRow(int id,double stars,double gap,bool v4=false) {
    auto leaderboard=FreshClanRanking(id,stars,100,v4);
    std::ostringstream row; row << std::setprecision(17) << "{\"leaderboardId\":\"" << id
        << "\",\"pp\":" << gap << ",\"leaderboard\":" << leaderboard << '}';
    return row.str();
}
void LegacyClanSelection() {
    auto easy=ClanProjection(1,300); easy.stars=easy.difficulties[0].stars=.25; easy.priority=-30;
    auto replay=ClanProjection(2,400); replay.stars=replay.difficulties[0].stars=8.5; replay.currentPp=300; replay.priority=-10;
    auto modifierPb=ClanProjection(3,300); modifierPb.currentPp=301; modifierPb.priority=0;
    auto achieved=ClanProjection(4,300); achieved.currentPp=300; achieved.priority=-1;
    auto tooHard=ClanProjection(5,500); tooHard.stars=tooHard.difficulties[0].stars=8.5001; tooHard.priority=-.1;
    auto alternative=replay; alternative.leaderboardId="another-difficulty"; alternative.priority=-3;
    alternative.difficulties[0].name="Expert";
    auto selected=SelectLegacyClanRecommendations({easy,replay,modifierPb,achieved,tooHard,alternative},8.5,16);
    CHECK(selected.size()==2 && selected[0].leaderboardId=="another-difficulty" && selected[1].hash==easy.hash);
    CHECK(selected[0].stars==8.5 && selected[1].stars==.25); // Inclusive maximum, no minimum.
    CHECK(SelectLegacyClanRecommendations({easy,replay},8.5,1)[0].hash==replay.hash);
    CHECK(SelectLegacyClanRecommendations({easy},0,16).empty());
    CHECK(SelectLegacyClanRecommendations({achieved,modifierPb},8.5,16).empty());
    std::cout << "PASS: legacy signed-gap ordering, maximum-only cap, PB exclusion and chart-before-song filtering\n";
}

void LegacyClanRefresh() {
    auto history=ClanTestHistory();
    auto modified=Parse(Chart(1,7,true,"FS"))[0]; modified.pp=9999; history.scores.push_back(modified);
    auto replay=Score(5,100); replay.leaderboardId="5"; history.scores.push_back(replay);
    auto noFail=Score(7,9999); noFail.noFail=true; history.scores.push_back(noFail);
    auto achieved=Score(3); achieved.leaderboardId="3";
    achieved.pp=EvaluatePlayerPerformance({achieved},history.scores)[0].predictedPp;
    history.scores.push_back(achieved);
    const double cap=EstimateClanMaxStars(history.scores,.5,history.attempts);
    std::string error,warning;
    ClanSearchState oldSearch; oldSearch.primaryClanId=1; oldSearch.cursor=37; oldSearch.recommendations={"keep"};
    CHECK(PersistClanSearchState("123","A",oldSearch,error));
    const auto before=ReadFile(ClanCursorPath("123","A").string());
    const auto png=ReadFile("tests/fixtures/clan-icon-live.png");
    int pages=0,profiles=0,icons=0,mode=0;
    HttpGetter get=[&](const std::string& url,std::string& body,std::string& failure) {
        if (url.find("/player/")!=std::string::npos) {
            ++profiles; body=R"({"clans":[{"id":1,"tag":"A","icon":"https://example.test/a.png"},{"id":2,"tag":"B"}],"clanOrder":"A,B"})";
        } else {
            CHECK(url.find("/clan/")!=std::string::npos && url.find("sortBy=toconquer&order=desc")!=std::string::npos);
            CHECK(url.find("count=500")!=std::string::npos); ++pages;
            if (url.find("/clan/B/")!=std::string::npos) body=Page(1,1,{LegacyClanRow(9,7,-1)});
            else if (mode==7) body=Page(1,1,{LegacyClanRow(1,7,0)});
            else if (url.ends_with("page=1")) body=Page(1,7,{LegacyClanRow(1,7,0),LegacyClanRow(2,cap+1,-1),LegacyClanRow(3,7,-2)});
            else if (mode==1) { failure="HTTP 503"; return false; }
            else if (mode==2) body="broken";
            else if (mode==3) body=Page(2,7,{});
            else if (mode==4) body=Page(2,7,{LegacyClanRow(1,7,0),LegacyClanRow(5,7,-10),LegacyClanRow(4,.25,-20),LegacyClanRow(7,7,-15)});
            else if (mode==5) body=Page(3,7,{LegacyClanRow(6,7,-3,true),LegacyClanRow(5,7,-10),LegacyClanRow(4,.25,-20),LegacyClanRow(7,7,-15)});
            else if (mode==6) body=Page(2,8,{LegacyClanRow(6,7,-3,true),LegacyClanRow(5,7,-10),LegacyClanRow(4,.25,-20),LegacyClanRow(7,7,-15)});
            else body=Page(2,7,{LegacyClanRow(6,7,-3,true),LegacyClanRow(5,7,-10),LegacyClanRow(4,.25,-20),LegacyClanRow(7,7,-15)});
        }
        return true;
    };
    HttpGetter icon=[&](const std::string& url,std::string& body,std::string&) {
        CHECK(url=="https://example.test/a.png"); ++icons; body=png; return true;
    };
    ClanScreenCache cache(false);
    std::vector<ClanPlaylistRecommendations> lists;
    CHECK(FetchClanToConquerInternal("123",true,16,lists,error,get,icon,.5,&history,false,&cache,true));
    CHECK(profiles==1 && pages==3 && icons==1 && lists.size()==2);
    CHECK(lists[0].legacy && lists[0].legacyMaxStars==cap && !lists[0].hypothetical);
    CHECK(lists[1].legacy && lists[1].hypothetical && lists[1].entries.size()==1);
    CHECK(lists[0].entries.size()==4 && lists[0].error.empty());
    const auto& entries=lists[0].entries;
    CHECK(entries[0].leaderboardId=="6" && entries[1].leaderboardId=="5" && entries[2].leaderboardId=="7" && entries[3].leaderboardId=="4");
    CHECK(entries[0].difficulties[0].noteSpan==120 && !entries[1].difficulties[0].noteSpan);
    for (const auto& entry:entries) {
        CHECK(entry.pp==0 && !entry.clanImpact && entry.predictedPp>entry.currentPp);
        CHECK(std::abs(entry.predictedPp-PredictBeatLeaderPp(entry.predictedAccuracy,entry.difficulties[0]))<1e-8);
    }
    CHECK(entries[1].currentPp==100 && entries[1].isImprovement);
    CHECK(entries[2].currentPp==0 && !entries[2].isImprovement); // No Fail isn't an eligible PB.
    CHECK(entries[3].stars==.25); // Far below the former automatic minimum.
    CHECK(!lists[0].cursorReady && lists[0].recheckedCharts==0 && lists[0].learnedCharts==0);
    CHECK(ReadFile(ClanCursorPath("123","A").string())==before);
    CHECK(cache.entries.empty()); // Legacy mode never downloads the full catalog or personalized screening totals.
    CHECK(FetchClanToConquerInternal("123",false,1,lists,error,get,icon,.95,&history,false,&cache,true));
    CHECK(lists.size()==1 && lists[0].entries.size()==1);
    CHECK(lists[0].legacyMaxStars==EstimateClanMaxStars(history.scores,.95,history.attempts));
    CHECK(lists[0].legacyMaxStars<=cap);
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,get,icon,0,&history,false,&cache,true));
    CHECK(std::isinf(lists[0].legacyMaxStars));
    CHECK(std::any_of(lists[0].entries.begin(),lists[0].entries.end(),[](const auto& e){return e.leaderboardId=="2";}));
    for(mode=1;mode<=6;++mode) {
        CHECK(FetchClanToConquerInternal("123",true,16,lists,error,get,icon,.5,&history,false,&cache,true));
        CHECK(!lists[0].error.empty() && lists[0].entries.empty() && lists[1].error.empty());
        CHECK(lists[1].entries.size()==1); // One failed clan doesn't stop another.
    }
    mode=7;
    CHECK(FetchClanToConquerInternal("123",false,16,lists,error,get,icon,.5,&history,false,&cache,true));
    CHECK(lists[0].error.empty() && lists[0].entries.empty()); // Successful empty PB-filtered result.
    auto empty=history; empty.scores.clear();
    CHECK(!FetchClanToConquerInternal("123",false,16,lists,error,get,icon,.5,&empty,false,&cache,true));
    CHECK(error.find("eligible ranked Standard score")!=std::string::npos);
    std::filesystem::remove(ClanCursorPath("123","A"));
    std::filesystem::remove(std::filesystem::path(RANKEDTOOLS_CLAN_CURSOR_DIRECTORY));
    RefreshSettings settings; CHECK(!settings.legacyClanPlaylists);
    settings.legacyClanPlaylists=true; auto snapshot=settings; settings.legacyClanPlaylists=false;
    std::thread worker([snapshot]{CHECK(snapshot.legacyClanPlaylists);}); worker.join();
    std::cout << "PASS: optional legacy clan refresh, automatic cap, later-page improvements, modifier/NF PBs, PPv3/v4, independent failures and mode history isolation\n";
}

void LegacyClearRateCap() {
    auto scores=ClanTestHistory().scores;
    ClearabilityModel clear(scores);
    double previous=std::numeric_limits<double>::infinity();
    const auto oldPersonalRange=MakeBeatLeaderNotPlayedStarRange(scores,.5);
    for(int percent=5;percent<=95;percent+=5) {
        const double rate=percent/100.;
        const double cap=EstimateClanMaxStars(scores,rate);
        CHECK(cap>=0 && std::isfinite(cap) && cap<=previous+1e-8);
        CHECK(clear.Predict(cap)>=rate-1e-8);
        CHECK(clear.Predict(cap+.001)<rate);
        previous=cap;
    }
    CHECK(std::isinf(EstimateClanMaxStars(scores,0)));
    const auto cap=EstimateClanMaxStars(scores,.5);
    CHECK(std::abs(cap-oldPersonalRange.maxStars)>.1); // No personal-PP window padding.
    auto higherPp=scores; for(auto& e:higherPp) e.pp*=100;
    CHECK(std::abs(EstimateClanMaxStars(higherPp,.5)-cap)<1e-8); // PP optimization cannot set the cap.
    CHECK(MakeBeatLeaderNotPlayedStarRange(scores,.5).maxStars==oldPersonalRange.maxStars);
    std::vector<Attempt> failures;
    const double now=ClanCacheNow();
    for(int i=0;i<20;++i) {
        Attempt a; a.id="cap-fail-"+std::to_string(i); a.source="local"; a.hash=Hash(50000+i);
        a.characteristic="Standard"; a.difficulty="ExpertPlus"; a.timestamp=now;
        a.stars=7; a.outcome=AttemptOutcome::Fail; failures.push_back(a);
    }
    CHECK(EstimateClanMaxStars(scores,.5,failures)<cap);
    // A valley in local clearability must not hide a higher qualifying region.
    scores.clear();
    for(int i=0;i<24;++i) {
        auto score=Score(60000+i); score.stars=score.difficulties[0].stars=i<20?7:12; scores.push_back(score);
    }
    for(auto& a:failures) a.stars=9;
    ClearabilityModel islands(scores,failures);
    CHECK(islands.Predict(9)<.5 && islands.Predict(12)>.5);
    const double highest=EstimateClanMaxStars(scores,.5,failures);
    CHECK(highest>12 && islands.Predict(highest)>=.5-1e-8 && islands.Predict(highest+.001)<.5);
    for(double stars=highest+.1;stars<=20;stars+=.1) CHECK(islands.Predict(stars)<.5);
    auto hard=ClanProjection(20,500); hard.stars=hard.difficulties[0].stars=30;
    CHECK(SelectLegacyClanRecommendations({hard},std::numeric_limits<double>::infinity(),16).size()==1);
    std::cout << "PASS: direct legacy clear-rate cap, all thresholds, zero-rate disable, PP independence, failure evidence and disconnected clearable regions\n";
}
