// Private legacy-mode implementation. Shares membership, score history and
// request pacing with personalized clan refreshes, but keeps server gap order.
bool FetchLegacyClanPlaylists(const std::vector<ClanMemberInfo>& clans, int primaryId, int limit,
                              double minClearRate, const ScoreHistory& history,
                              std::vector<ClanPlaylistRecommendations>& playlists,
                              const HttpGetter& get, const HttpGetter& getIcon) {
    const double maxStars=EstimateClanMaxStars(history.scores,minClearRate,history.attempts);
    for (const auto& clan:clans) {
        ClanPlaylistRecommendations playlist;
        playlist.clanTag=clan.tag; playlist.legacy=true; playlist.legacyMaxStars=maxStars;
        playlist.hypothetical=clan.id!=primaryId;
        std::string icon=clan.icon;
        std::vector<MapEntry> candidates;
        std::unordered_set<std::string> seen;
        double reportedTotal=-1;
        std::size_t visited=0;
        const bool complete=FetchPages<MapEntry>([&](int page) {
            return "https://api.beatleader.com/clan/"+ClanUrlComponent(clan.tag)+"/maps?count="+
                std::to_string(kClanBulkPageSize)+"&sortBy=toconquer&order=desc&page="+std::to_string(page);
        },candidates,playlist.error,[&](const auto& body,auto& entries,auto& error,PageInfo* info) {
            rapidjson::Document doc; doc.Parse(body.c_str());
            if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("data") || !doc["data"].IsArray() ||
                !doc.HasMember("metadata") || !doc["metadata"].IsObject()) {
                error="Invalid legacy clan map page for "+clan.tag+"."; return false;
            }
            *info={}; info->rawRows=doc["data"].Size();
            if (!ReadClanNumber(doc["metadata"],"total",info->total) || info->total<0 || std::floor(info->total)!=info->total ||
                !ReadClanNumber(doc["metadata"],"page",info->page) || info->page<1 || std::floor(info->page)!=info->page ||
                (reportedTotal>=0 && reportedTotal!=info->total) || visited+info->rawRows>info->total) {
                error="Incomplete or changing legacy clan map pagination."; return false;
            }
            reportedTotal=info->total; visited+=info->rawRows;
            if (doc.HasMember("container")) {
                std::string tag;
                if (!ReadString(doc["container"],"tag",tag) || tag!=clan.tag) {
                    error="Legacy clan maps belong to a different clan."; return false;
                }
                ReadString(doc["container"],"icon",icon);
            }
            for (const auto& row:doc["data"].GetArray()) {
                double gap=0;
                if (!ReadClanNumber(row,"pp",gap) || !row.HasMember("leaderboard") || !row["leaderboard"].IsObject() ||
                    !row["leaderboard"].HasMember("song") || !row["leaderboard"]["song"].IsObject() ||
                    !row["leaderboard"].HasMember("difficulty") || !row["leaderboard"]["difficulty"].IsObject()) {
                    error="Incomplete legacy clan chart data."; return false;
                }
                const auto& leaderboard=row["leaderboard"];
                Context context;
                UpdateContext(leaderboard["song"],context); UpdateContext(leaderboard["difficulty"],context);
                ReadString(row,"leaderboardId",context.leaderboardId);
                if (context.leaderboardId.empty()) ReadString(leaderboard,"id",context.leaderboardId);
                if (context.leaderboardId.empty() || !seen.insert(context.leaderboardId).second) {
                    error="Missing or repeated legacy clan leaderboard."; return false;
                }
                const auto before=entries.size();
                if (context.ranked) AddIfComplete(context,entries);
                if (context.ranked && entries.size()==before) {
                    error="Incomplete legacy clan chart metadata."; return false;
                }
                if (entries.size()>before) entries.back().priority=gap;
            }
            return true;
        },get,false);
        if (!complete) { playlists.push_back(std::move(playlist)); continue; }
        auto evaluated=EvaluatePlayerPerformance(candidates,history.scores,history.attempts);
        playlist.checkedCharts=candidates.size(); playlist.eligibleCharts=evaluated.size();
        const auto standardCharts=std::count_if(candidates.begin(),candidates.end(),[](const auto& e) {
            return Lower(e.difficulties.front().characteristic)=="standard";
        });
        if (evaluated.size()<static_cast<std::size_t>(standardCharts))
            playlist.warning="Some legacy charts had no usable personal PP estimate.";
        playlist.entries=SelectLegacyClanRecommendations(std::move(evaluated),maxStars,limit);
        if (playlist.entries.empty() && !playlist.warning.empty()) {
            playlist.error="Legacy clan PP estimates were incomplete; the previous playlist was preserved.";
            playlists.push_back(std::move(playlist)); continue;
        }
        std::string image,error;
        if (Lower(icon).starts_with("https://") && getIcon(icon,image,error) && image.size()<=kMaxClanIconBytes)
            playlist.image=DownloadedPlaylistImage(image);
        if (playlist.image.empty()) playlist.iconWarning="Clan icon unavailable for "+clan.tag+"; using the default cover.";
        playlists.push_back(std::move(playlist));
    }
    return true;
}
