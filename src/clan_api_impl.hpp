// Included inside recommendation_api.cpp's private namespace. Shares its
// injected HTTP boundary, parsing, pagination and refresh-owned score history.
struct ClanMemberInfo { int id; std::string tag, icon; };
struct ClanTotal { double pp; int rank; };
using ClanTotals = std::unordered_map<std::string, ClanTotal>;
struct ClanOwnership { std::vector<std::pair<int, double>> clans; };
bool ReadClanNumber(const rapidjson::Value& row, const char* key, double& value) {
    if (!row.IsObject() || !row.HasMember(key) || !row[key].IsNumber() || !std::isfinite(row[key].GetDouble())) return false;
    value = row[key].GetDouble(); return true;
}
#include "clan_refresh_cache.hpp"

ClanScreenCache& SharedClanScreenCache() { static ClanScreenCache cache; return cache; }

std::string ClanUrlComponent(const std::string& value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') result += c;
        else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
    }
    return result;
}
std::filesystem::path ClanCursorPath(const std::string& id, const std::string& tag) {
    if (!IsDigits(id)) throw std::runtime_error("Invalid clan-search player ID.");
    // Hex encoding avoids filesystem special names, traversal and escaped separators.
    std::string encoded;
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : tag) { encoded += hex[c >> 4]; encoded += hex[c & 15]; }
    if (encoded.empty() || encoded.size() > 256) throw std::runtime_error("Invalid clan-search tag.");
    return RuntimePaths().clanSearch / (id + "_" + encoded + ".json");
}
#include "clan_search_state.hpp"
std::size_t ReadClanCursor(const std::string& id, const std::string& tag, std::string& warning) {
    return ReadClanSearchState(id,tag,warning).cursor;
}
bool PersistClanCursor(const std::string& id, const std::string& tag, std::size_t cursor, std::string& error) {
    std::string warning;
    auto state=ReadClanSearchState(id,tag,warning); state.cursor=cursor;
    return PersistClanSearchState(id,tag,state,error);
}
bool ParseClanTotals(const std::string& body, std::vector<std::pair<std::string, ClanTotal>>& rows,
                      std::string& error, PageInfo* page, const std::string& tag, std::string& icon) {
    rapidjson::Document doc; doc.Parse(body.c_str());
    if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("data") || !doc["data"].IsArray()) {
        error = "Invalid clan map totals for " + tag + "."; return false;
    }
    *page = {};
    page->rawRows = doc["data"].Size();
    if (doc.HasMember("metadata") && doc["metadata"].IsObject()) {
        const auto& meta = doc["metadata"];
        if (!ReadClanNumber(meta, "total", page->total) || page->total < 0 || std::floor(page->total) != page->total ||
            !ReadClanNumber(meta, "page", page->page) || page->page < 1 || std::floor(page->page) != page->page) {
            error = "Invalid clan map pagination metadata."; return false;
        }
    }
    if (doc.HasMember("container") && doc["container"].IsObject()) {
        std::string containerTag;
        if (!ReadString(doc["container"], "tag", containerTag) || containerTag != tag) {
            error = "Clan map totals belong to a different clan."; return false;
        }
        ReadString(doc["container"], "icon", icon);
    }
    for (const auto& row : doc["data"].GetArray()) {
        std::string id; double pp = -1, rank = 0;
        if (!ReadString(row, "leaderboardId", id) || !ReadClanNumber(row, "pp", pp) || pp < 0 ||
            !ReadClanNumber(row, "rank", rank) || rank < 1 || rank > std::numeric_limits<int>::max() || std::floor(rank) != rank) {
            error = "Incomplete clan map total."; return false;
        }
        rows.push_back({id, {pp, static_cast<int>(rank)}});
    }
    return true;
}
bool FetchClanMapState(const std::string& leaderboardId, int clanId, ClanMapState& state,
                       std::string& error, const HttpGetter& get, bool pace) {
    state = {};
    std::unordered_set<std::string> players;
    std::string previous;
    double count = -1;
    for (int page = 1; ; ++page) {
        std::string body;
        if (!get("https://api.beatleader.com/leaderboard/clanRankings/" + ClanUrlComponent(leaderboardId) +
                 "/clan/" + std::to_string(clanId) + "?page=" + std::to_string(page) + "&count=100", body, error)) return false;
        // A missing clan ranking is a successful null/empty response, not an HTTP failure.
        if (page == 1 && (body.empty() || body == "null")) return true;
        rapidjson::Document doc; doc.Parse(body.c_str());
        double pp = -1, reportedCount = -1, id = 0;
        std::string returnedId;
        if (doc.HasParseError() || !doc.IsObject() || !ReadString(doc, "leaderboardId", returnedId) || returnedId != leaderboardId ||
            !ReadClanNumber(doc, "pp", pp) || pp < 0 || !ReadClanNumber(doc, "associatedScoresCount", reportedCount) ||
            reportedCount < 0 || std::floor(reportedCount) != reportedCount || reportedCount > 100000 ||
            !doc.HasMember("clan") || !ReadClanNumber(doc["clan"], "id", id) || id != clanId ||
            !doc.HasMember("associatedScores") || !doc["associatedScores"].IsArray() ||
            (page > 1 && (body == previous || count != reportedCount || std::abs(state.pp - pp) > .01))) {
            error = "Incomplete or changing clan contribution data."; return false;
        }
        count = reportedCount; state.pp = pp;
        for (const auto& row : doc["associatedScores"].GetArray()) {
            ClanScore score;
            if (!ReadString(row, "playerId", score.playerId) || !IsDigits(score.playerId) ||
                !ReadClanNumber(row, "pp", score.pp) || score.pp < 0 ||
                !ReadClanNumber(row, "accuracy", score.accuracy) || score.accuracy < 0 || score.accuracy > 1 ||
                !players.insert(score.playerId).second) { error = "Invalid or repeated clan contribution."; return false; }
            ReadTimestamp(row, "timepost", score.timepost);
            if (score.timepost <= 0) ReadTimestamp(row, "timeset", score.timepost);
            state.scores.push_back(std::move(score));
        }
        if (state.scores.size() >= count) {
            if (state.scores.size() != count || std::abs(WeightedClanPp(state.scores) - state.pp) > .05) {
                error = "Clan contributions do not match the reported total."; return false;
            }
            return true;
        }
        if (doc["associatedScores"].Empty()) { error = "Clan contributions ended before the reported count."; return false; }
        previous = std::move(body);
        if (pace) std::this_thread::sleep_for(std::chrono::milliseconds(90));
    }
}

#include "clan_legacy_api.hpp"

bool FetchClanToConquerInternal(const std::string& playerId, bool allClans,
                               int limit, std::vector<ClanPlaylistRecommendations>& playlists, std::string& error,
                               const HttpGetter& get = HttpGet, const HttpGetter& getIcon = HttpGetIcon,
                               double minClearRate = .5, ScoreHistory* cachedHistory = nullptr, bool pace = true,
                               ClanScreenCache* screeningCache = nullptr, bool legacy = false) {
    playlists.clear(); error.clear();
    if (!IsDigits(playerId)) { error = "Enter a numeric BeatLeader player ID to load clan maps."; return false; }
    minClearRate = std::isfinite(minClearRate) ? std::clamp(minClearRate, 0.0, .95) : .5;
    ClanRequestGate gate;
    const HttpGetter request = [&](const std::string& url, std::string& response, std::string& failure) {
        if (!gate.Acquire(pace,url)) { failure = "Leaderboard API returned HTTP 429; remaining checks stopped."; return false; }
        const bool ok = get(url,response,failure);
        if (!ok && failure.find("HTTP 429") != std::string::npos) gate.limited.store(true);
        return ok;
    };
    std::string body;
    if (!request("https://api.beatleader.com/player/" + playerId + "?stats=true", body, error)) return false;
    rapidjson::Document profile; profile.Parse(body.c_str());
    if (profile.HasParseError() || !profile.IsObject() || !profile.HasMember("clans") ||
        (!profile["clans"].IsArray() && !profile["clans"].IsNull())) {
        error = "BeatLeader profile did not include valid clan membership data."; return false;
    }
    std::vector<ClanMemberInfo> clans;
    if (profile["clans"].IsArray()) for (const auto& row : profile["clans"].GetArray()) {
        ClanMemberInfo clan{}; double id = 0;
        if (!ReadString(row, "tag", clan.tag) || clan.tag.empty() || !ReadClanNumber(row, "id", id) ||
            id <= 0 || id > std::numeric_limits<int>::max() || std::floor(id) != id) {
            error = "BeatLeader returned invalid clan membership."; return false;
        }
        clan.id = static_cast<int>(id); ReadString(row, "icon", clan.icon);
        if (std::none_of(clans.begin(), clans.end(), [&](const auto& c) { return c.id == clan.id; })) clans.push_back(clan);
    }
    if (clans.empty()) return true;
    std::string order; ReadString(profile, "clanOrder", order);
    auto position = [&](const auto& clan) {
        auto p = ("," + order + ",").find("," + clan.tag + ",");
        return p == std::string::npos ? std::numeric_limits<std::size_t>::max() : p;
    };
    std::sort(clans.begin(), clans.end(), [&](const auto& a, const auto& b) {
        return position(a) != position(b) ? position(a) < position(b) : a.id < b.id;
    });
    const int primaryId = clans.front().id;
    if (!allClans) clans.resize(1);
    ScoreHistory localHistory;
    auto& history = cachedHistory ? *cachedHistory : localHistory;
    if (!LoadScoreHistory(playerId, RatingSystem::BeatLeader, history, error, request, false)) return false;
    if (BuildAbilityChart(history.scores).scores.empty()) {
        error = "Clan recommendations need an eligible ranked Standard score (at least 55% accuracy, without No Fail)."; return false;
    }
    if (legacy) return FetchLegacyClanPlaylists(clans,primaryId,limit,minClearRate,history,playlists,request,getIcon);
    std::vector<MapEntry> catalog;
    std::string cachedBody, cacheWarning, cacheError;
    PageInfo cacheInfo;
    const bool cachedCatalog = screeningCache && screeningCache->Get("catalog",kClanCatalogCacheSeconds,ClanCacheNow(),cachedBody) &&
        ParseEntries(cachedBody,catalog,cacheError,&cacheInfo);
    if (!cachedCatalog) {
        catalog.clear();
        if (!FetchPages<MapEntry>([](int page) {
            return "https://api.beatleader.com/maps?type=ranked&mode=Standard&count=" + std::to_string(kClanBulkPageSize) +
                "&sortBy=stars&order=desc&page=" + std::to_string(page);
        }, catalog, error, ParseEntries, request, false)) return false;
        if (screeningCache && !screeningCache->Put("catalog",CompactClanCatalog(catalog),ClanCacheNow()))
            cacheWarning = "Screening cache could not be saved; future refreshes may be slower.";
    }
    auto evaluated = EvaluatePlayerPerformance(catalog, history.scores, history.attempts);
    std::unordered_set<std::string> chartIds;
    evaluated.erase(std::remove_if(evaluated.begin(), evaluated.end(), [&](const auto& e) {
        return e.leaderboardId.empty() || e.clearChance < minClearRate ||
            e.predictedPp <= e.currentPp || !chartIds.insert(e.leaderboardId).second;
    }), evaluated.end());
    if (!request("https://api.beatleader.com/clans/globalmap", body, error)) return false;
    rapidjson::Document global; global.Parse(body.c_str());
    if (global.HasParseError() || !global.IsObject() || !global.HasMember("points") || !global["points"].IsArray()) {
        error = "BeatLeader returned invalid clan ownership data."; return false;
    }
    std::unordered_map<std::string, ClanOwnership> ownership;
    for (const auto& point : global["points"].GetArray()) {
        std::string id;
        if (!ReadString(point, "leaderboardId", id) || !point.HasMember("clans") || !point["clans"].IsArray()) continue;
        ClanOwnership row;
        for (const auto& c : point["clans"].GetArray()) {
            double clanId = 0, pp = -1;
            if (ReadClanNumber(c, "id", clanId) && clanId > 0 && clanId <= std::numeric_limits<int>::max() && std::floor(clanId) == clanId &&
                ReadClanNumber(c, "pp", pp) && pp >= 0) row.clans.push_back({static_cast<int>(clanId), pp});
        }
        if (!row.clans.empty()) ownership[id] = std::move(row);
    }
    // Cache verified responses only within this refresh, shared across all clans.
    std::unordered_map<std::string, ClanMapState> stateCache;
    struct LiveClanRanking { std::vector<std::pair<int,double>> clans; MapEntry chart; };
    std::unordered_map<std::string, LiveClanRanking> rankingCache;
    std::mutex responseCacheMutex;
    auto stateFor = [&](const std::string& lb, int clan, ClanMapState& out, std::string& failure) {
        const auto key = lb + ":" + std::to_string(clan);
        { std::lock_guard<std::mutex> lock(responseCacheMutex);
          auto found = stateCache.find(key);
          if (found != stateCache.end()) { out = found->second; return true; } }
        if (!FetchClanMapState(lb, clan, out, failure, request, false)) return false;
        { std::lock_guard<std::mutex> lock(responseCacheMutex); stateCache[key] = out; }
        return true;
    };
    for (const auto& clan : clans) {
        ClanPlaylistRecommendations playlist; playlist.clanTag = clan.tag; playlist.hypothetical = clan.id != primaryId;
        playlist.warning = cacheWarning;
        std::vector<std::pair<std::string, ClanTotal>> totalsRows;
        std::string icon = clan.icon;
        const std::string totalsKey = "totals_" + std::to_string(clan.id);
        PageInfo cachedPage;
        const bool cachedTotals = screeningCache && screeningCache->Get(totalsKey,kClanTotalsCacheSeconds,ClanCacheNow(),cachedBody) &&
            ParseClanTotals(cachedBody,totalsRows,cacheError,&cachedPage,clan.tag,icon);
        bool loaded = cachedTotals;
        if (!cachedTotals) {
          totalsRows.clear();
          loaded = FetchPages<std::pair<std::string, ClanTotal>>([&](int page) {
            return "https://api.beatleader.com/clan/" + ClanUrlComponent(clan.tag) +
                "/maps?count=" + std::to_string(kClanBulkPageSize) + "&sortBy=pp&order=desc&page=" + std::to_string(page);
        }, totalsRows, playlist.error, [&](const auto& json, auto& rows, auto& failure, auto* info) {
            return ParseClanTotals(json, rows, failure, info, clan.tag, icon);
          }, request, false);
        }
        ClanTotals totals;
        for (const auto& [id, total] : totalsRows) if (!totals.emplace(id, total).second)
            playlist.error = "BeatLeader repeated a clan map total; the previous playlist was preserved.";
        if (!loaded || !playlist.error.empty()) { playlists.push_back(std::move(playlist)); continue; }
        if (!cachedTotals && screeningCache && !screeningCache->Put(totalsKey,CompactClanTotals(totalsRows,clan.tag,icon),ClanCacheNow()))
            playlist.warning += " Clan screening totals could not be cached.";
        std::string searchWarning;
        auto search=ReadClanSearchState(playerId,clan.tag,searchWarning);
        if (search.primaryClanId!=primaryId) { search.recommendations.clear(); search.evidence.clear(); }
        search.primaryClanId=primaryId;
        if (!searchWarning.empty()) playlist.warning += " "+searchWarning;
        std::unordered_map<std::string,const ClanSearchEvidence*> previousEvidence;
        for (const auto& e:search.evidence) previousEvidence.emplace(e.leaderboardId,&e);
        const std::unordered_set<std::string> previousRecommendations(search.recommendations.begin(),search.recommendations.end());
        std::unordered_map<std::string,std::pair<double,double>> screenedTotals;
        std::vector<ClanScreenCandidate> candidates;
        for (const auto& chart : evaluated) {
            const double ownPp = totals.contains(chart.leaderboardId) ? totals.at(chart.leaderboardId).pp : 0;
            double rival = -1;
            if (const auto found = ownership.find(chart.leaderboardId); found != ownership.end())
                for (const auto& [id, pp] : found->second.clans) if (id != clan.id) rival = std::max(rival, pp);
            if (rival >= 0 && ownPp > rival + 1e-8 &&
                totals.contains(chart.leaderboardId) && totals.at(chart.leaderboardId).rank == 1) continue;
            const auto found=previousEvidence.find(chart.leaderboardId);
            const auto* evidence=found==previousEvidence.end()?nullptr:found->second;
            if (evidence && ClanEvidenceMatches(*evidence,chart,ownPp,rival)) ++playlist.learnedCharts;
            // Unknown ownership stays unknown until verified. Prior weighted
            // impact is a screening hint only; every selected chart is checked live.
            candidates.push_back(ScreenClanCandidate(chart,ownPp,rival,minClearRate,evidence));
            screenedTotals.emplace(chart.leaderboardId,std::pair{ownPp,rival});
        }
        playlist.eligibleCharts = candidates.size();
        std::size_t cursor=search.cursor;
        const std::size_t originalCursor = cursor;
        auto shortlisted = ShortlistClanCandidates(std::move(candidates),ClanVerificationBudget(limit),cursor,search.recommendations);
        struct Verification {
            bool attempted{false}, valid{false};
            ClanMapInput input;
            std::string error;
            HttpStatistics extra;
        };
        std::vector<Verification> checks(shortlisted.size());
        const auto parentThread = std::this_thread::get_id();
        RunClanJobs(shortlisted.size(),pace,[&](std::size_t index) {
            if (gate.limited.load()) return;
            auto& checked = checks[index]; checked.attempted = true;
            const auto before = GetHttpStatistics();
            const auto& candidate = shortlisted[index];
            const auto& lb = candidate.chart.leaderboardId;
            const auto verify = [&] {
                LiveClanRanking live;
                bool cached = false;
                { std::lock_guard<std::mutex> lock(responseCacheMutex);
                  const auto found = rankingCache.find(lb);
                  if (found != rankingCache.end()) { live = found->second; cached = true; } }
                if (!cached) {
                    std::string json;
                    if (!request("https://api.beatleader.com/leaderboard/clanRankings/" + ClanUrlComponent(lb) + "?count=3",json,checked.error)) return;
                    rapidjson::Document doc; doc.Parse(json.c_str());
                    std::string returnedId;
                    if (doc.HasParseError() || !doc.IsObject() || !ReadString(doc,"id",returnedId) || returnedId != lb ||
                        !doc.HasMember("clanRanking") || !doc["clanRanking"].IsArray()) return;
                    for (const auto& row : doc["clanRanking"].GetArray()) {
                        double id = 0, pp = -1;
                        if (!row.IsObject() || !row.HasMember("clan") || !ReadClanNumber(row["clan"],"id",id) || id <= 0 ||
                            id > std::numeric_limits<int>::max() || std::floor(id) != id || !ReadClanNumber(row,"pp",pp) || pp < 0 ||
                            std::any_of(live.clans.begin(),live.clans.end(),[&](const auto& r) { return r.first == id; })) return;
                        live.clans.push_back({static_cast<int>(id),pp});
                    }
                    if (!std::is_sorted(live.clans.begin(),live.clans.end(),[](const auto& x,const auto& y) { return x.second > y.second; })) return;
                    live.chart = candidate.chart;
                    // A cached catalog is only a screening hint. Use the fresh
                    // leaderboard's ratings/status/hash for every final projection.
                    if (doc.HasMember("song") && doc.HasMember("difficulty")) {
                        Context context;
                        UpdateContext(doc["song"],context); UpdateContext(doc["difficulty"],context);
                        context.leaderboardId = lb;
                        std::vector<MapEntry> charts;
                        if (context.ranked) AddIfComplete(context,charts);
                        if (charts.size() != 1 || Lower(charts[0].hash) != Lower(candidate.chart.hash)) return;
                        live.chart = std::move(charts[0]);
                    } else if (screeningCache) return;
                    { std::lock_guard<std::mutex> lock(responseCacheMutex); rankingCache[lb] = live; }
                }
                auto& input = checked.input;
                input.chart = live.chart; input.hypothetical = playlist.hypothetical;
                if (!stateFor(lb,clan.id,input.clan,checked.error)) return;
                if (input.hypothetical && !stateFor(lb,primaryId,input.primaryClan,checked.error)) return;
                for (const auto& [id,pp] : live.clans)
                    if (id != clan.id && (!input.hypothetical || id != primaryId)) input.rivalPp = std::max(input.rivalPp,pp);
                input.held = !live.clans.empty() && live.clans.front().first == clan.id &&
                    (live.clans.size() == 1 || live.clans.front().second > live.clans[1].second);
                for (const auto& [id,pp] : live.clans) {
                    if (id == clan.id && std::abs(pp-input.clan.pp) > .05) return;
                    if (input.hypothetical && id == primaryId && std::abs(pp-input.primaryClan.pp) > .05) return;
                }
                checked.valid = true;
            };
            verify();
            const auto after = GetHttpStatistics();
            if (std::this_thread::get_id() != parentThread)
                checked.extra = {after.requests-before.requests,after.connections-before.connections,after.seconds-before.seconds};
        });
        std::vector<MapEntry> freshCharts;
        for (std::size_t i=0;i<checks.size();++i) {
            const auto& checked=checks[i];
            httpStatistics.requests += checked.extra.requests;
            httpStatistics.connections += checked.extra.connections;
            httpStatistics.seconds += checked.extra.seconds;
            if (!checked.attempted) continue;
            ++playlist.checkedCharts;
            if (previousRecommendations.contains(shortlisted[i].chart.leaderboardId)) ++playlist.recheckedCharts;
            if (!checked.valid) ++playlist.failedCharts;
            else freshCharts.push_back(checked.input.chart);
        }
        auto freshPerformance = EvaluatePlayerPerformance(freshCharts,history.scores,history.attempts);
        std::unordered_map<std::string,MapEntry> performanceById;
        for (auto& entry : freshPerformance) performanceById.emplace(entry.leaderboardId,std::move(entry));
        std::vector<MapEntry> verified;
        std::vector<ClanSearchEvidence> learned;
        std::unordered_set<std::string> updated;
        for (std::size_t i=0;i<checks.size();++i) {
            auto& checked=checks[i];
            if (!checked.valid) continue;
            const auto found = performanceById.find(checked.input.chart.leaderboardId);
            if (found == performanceById.end()) continue;
            checked.input.chart = found->second;
            MapEntry entry;
            std::optional<ClanImpact> projection;
            const bool useful=EvaluateClanImpact(std::move(checked.input),playerId,minClearRate,entry,&projection);
            if (projection) {
                const auto& chart=shortlisted[i].chart;
                const auto [ownPp,rivalPp]=screenedTotals.at(chart.leaderboardId);
                learned.push_back({chart.leaderboardId,ownPp,rivalPp,chart.currentPp,chart.predictedPp,chart.predictedAccuracy,
                    projection->currentClanPp,projection->rivalPp,projection->gain,useful});
                updated.insert(chart.leaderboardId);
            }
            if (useful) verified.push_back(std::move(entry));
        }
        if (gate.limited.load()) {
            if (!playlist.warning.empty()) playlist.warning += " ";
            playlist.warning += "BeatLeader rate limited map verification; remaining checks stopped.";
            cursor = originalCursor;
        }
        playlist.entries = SelectClanRecommendations(std::move(verified), limit);
        if ((playlist.failedCharts || gate.limited.load()) && playlist.entries.empty())
            playlist.error = "Map verification was incomplete; the previous clan playlist was preserved.";
        playlist.nextCursor = cursor; playlist.cursorReady = playlist.error.empty();
        search.cursor=cursor;
        search.evidence.erase(std::remove_if(search.evidence.begin(),search.evidence.end(),[&](const auto& e) {
            return !chartIds.contains(e.leaderboardId) || updated.contains(e.leaderboardId);
        }),search.evidence.end());
        search.evidence.insert(search.evidence.end(),learned.begin(),learned.end());
        if (search.evidence.size()>kMaxClanSearchEvidence)
            search.evidence.erase(search.evidence.begin(),search.evidence.begin()+(search.evidence.size()-kMaxClanSearchEvidence));
        search.recommendations.clear();
        for (const auto& entry:playlist.entries) search.recommendations.push_back(entry.leaderboardId);
        // A transient failed check must not make us forget to retry a previous pick.
        for (std::size_t i=0;i<checks.size() && search.recommendations.size()<128;++i)
            if (!checks[i].valid && previousRecommendations.contains(shortlisted[i].chart.leaderboardId))
                search.recommendations.push_back(shortlisted[i].chart.leaderboardId);
        playlist.searchState=std::move(search);
        if (playlist.checkedCharts < playlist.eligibleCharts || playlist.failedCharts) {
            if (!playlist.warning.empty()) playlist.warning += " ";
            playlist.warning += "Approximate search: checked " + std::to_string(playlist.checkedCharts) + " of " +
                std::to_string(playlist.eligibleCharts) + " eligible charts; " + std::to_string(playlist.failedCharts) + " verification failures.";
        }
        std::string image, imageError;
        if (Lower(icon).starts_with("https://") && getIcon(icon, image, imageError) && image.size() <= kMaxClanIconBytes)
            playlist.image = DownloadedPlaylistImage(image);
        if (playlist.image.empty()) playlist.iconWarning = "Clan icon unavailable for " + clan.tag + "; using the default cover.";
        playlists.push_back(std::move(playlist));
    }
    return true;
}
