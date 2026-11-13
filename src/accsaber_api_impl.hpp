// Private adapter, included after the existing history loader. Public API calls
// are GET-only; every cache belongs to one refresh, not a saved rating epoch.
constexpr const char* kAccApi = "https://api.accsaber.com/v1";
std::string AccText(const rapidjson::Value& value, const char* key) {
    if (!value.IsObject() || !value.HasMember(key)) return {};
    const auto& field = value[key];
    if (field.IsString()) return {field.GetString(), field.GetStringLength()};
    if (field.IsUint64()) return std::to_string(field.GetUint64());
    return {};
}
bool AccNumber(const rapidjson::Value& value, const char* key, double& result) {
    if (!value.IsObject() || !value.HasMember(key) || !value[key].IsNumber() || !std::isfinite(value[key].GetDouble())) return false;
    result = value[key].GetDouble(); return true;
}
bool AccGet(const std::string& path, rapidjson::Document& doc, std::string& error, const HttpGetter& get) {
    std::string body;
    if (!get(std::string(kAccApi) + path, body, error)) {
        error = "AccSaber request " + path + " failed: " + (error.empty() ? "No response details were available." : error);
        return false;
    }
    doc.Parse(body.data(), body.size());
    if (doc.HasParseError()) { error = "AccSaber returned invalid JSON for " + path + "."; return false; }
    return true;
}
bool AccPages(const std::string& path, const std::function<bool(const rapidjson::Value&, std::string&)>& parse,
              std::string& error, const HttpGetter& get, bool pace) {
    double expectedTotal = -1, expectedPages = -1, received = 0;
    std::unordered_set<std::string> ids;
    for (int page = 0; ; ++page) {
        rapidjson::Document doc;
        const auto requestPath = path + (path.find('?') == std::string::npos ? "?" : "&") +
            "size=100&page=" + std::to_string(page);
        const auto fail = [&](const std::string& message) {
            error = message + " [" + requestPath + "]"; return false;
        };
        if (!AccGet(requestPath, doc, error, get)) return false;
        if (!doc.IsObject() || !doc.HasMember("content") || !doc["content"].IsArray()) {
            return fail("AccSaber returned an unexpected paginated response.");
        }
        const auto& meta = doc.HasMember("page") && doc["page"].IsObject() ? doc["page"] : doc;
        double number, total, pages;
        if (!AccNumber(meta, "number", number) || number != page || !AccNumber(meta, "totalElements", total) ||
            !AccNumber(meta, "totalPages", pages) || total < 0 || pages < 0 || total != std::floor(total) ||
            pages != std::floor(pages) || ((total == 0) != (pages == 0)) || (pages > 0 && number >= pages) ||
            (page > 0 && (expectedTotal != total || expectedPages != pages))) {
            return fail("AccSaber pagination changed or is incomplete; previous playlist preserved.");
        }
        expectedTotal = total; expectedPages = pages;
        for (const auto& row : doc["content"].GetArray()) {
            const auto id = AccText(row, "id");
            if (id.empty() || !ids.insert(id).second || !parse(row, error)) {
                return fail(error.empty() ? "AccSaber returned a malformed or repeated row." : error);
            }
        }
        received += doc["content"].Size();
        if (received > total || (doc["content"].Empty() && received < total)) {
            return fail("AccSaber returned incomplete rows (received " + std::to_string(static_cast<size_t>(received)) +
                " of " + std::to_string(static_cast<size_t>(total)) + ").");
        }
        if (page + 1 >= pages) {
            if (received != total) return fail("AccSaber did not return the complete history/catalog.");
            return true;
        }
        if (page >= 100000) return fail("AccSaber pagination exceeds the supported range.");
        if (pace) std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
}
std::string AccDifficulty(const std::string& name) {
    const auto lower = Lower(name);
    return lower == "easy" ? "Easy" : lower == "normal" ? "Normal" : lower == "hard" ? "Hard" :
        lower == "expert" ? "Expert" : lower == "expert_plus" || lower == "expertplus" ? "ExpertPlus" : "";
}
bool AccIdentity(MapEntry& entry, const rapidjson::Value& row) {
    Difficulty d;
    d.characteristic = AccText(row, "characteristic"); d.name = AccDifficulty(AccText(row, "difficulty"));
    entry.difficulties = {d};
    return entry.hash.size() == 40 && std::all_of(entry.hash.begin(), entry.hash.end(), [](unsigned char c) { return std::isxdigit(c); }) && !d.characteristic.empty() && !d.name.empty();
}
bool AccCatalog(ScoreHistory& history, std::string& error, const HttpGetter& get, bool pace) {
    if (history.accCatalogLoaded) return true;
    std::vector<MapEntry> result;
    std::unordered_set<std::string> chartIds, keys;
    if (!AccPages("/maps?status=RANKED", [&](const auto& map, auto& failure) {
        if (!map.IsObject() || !map.HasMember("difficulties") || !map["difficulties"].IsArray()) return false;
        for (const auto& difficulty : map["difficulties"].GetArray()) {
            const auto status = AccText(difficulty, "status");
            if (status.empty()) return false;
            if (status != "RANKED") continue;
            MapEntry e;
            e.hash = Lower(AccText(map, "songHash")); e.songName = AccText(map, "songName");
            e.songSubName = AccText(map, "songSubName"); e.songAuthorName = AccText(map, "songAuthor");
            e.levelAuthorName = AccText(map, "mapAuthor"); e.leaderboardId = AccText(difficulty, "id");
            e.categoryId = AccText(difficulty, "categoryId");
            if (!AccIdentity(e, difficulty) || e.leaderboardId.empty() || !chartIds.insert(e.leaderboardId).second ||
                !keys.insert(EntryKey(e)).second) { failure = "AccSaber ranked chart identities are incomplete or repeated."; return false; }
            if (e.songName.empty()) e.songName = e.hash;
            double complexity;
            if (AccNumber(difficulty, "complexity", complexity) && complexity >= 0 && complexity <= 14) {
                e.difficulties[0].complexity = complexity; e.difficulties[0].stars = e.stars = complexity;
            }
            AccNumber(difficulty, "maxScore", e.maxScore);
            for (const auto& category : history.accCategories) if (category.id == e.categoryId) e.categoryCode = category.code;
            result.push_back(std::move(e));
        }
        return true;
    }, error, get, pace)) return false;
    history.accCatalog = std::move(result); history.accCatalogLoaded = true; return true;
}
bool AccCurve(const rapidjson::Value& input, bool weight, AccSaberCurve& curve,
              std::string& error, const HttpGetter& get) {
    curve.id = AccText(input, "id");
    rapidjson::Document fetched;
    const rapidjson::Value* value = &input;
    if (!weight && (!input.IsObject() || !input.HasMember("points") || !input["points"].IsArray())) {
        if (curve.id.empty() || !AccGet("/curves/" + curve.id, fetched, error, get)) return false;
        value = &fetched;
    }
    if (weight) {
        if (AccText(*value, "type") != "FORMULA" || AccText(*value, "formula") != "LOGISTIC_SIGMOID" || !AccNumber(*value, "xParameterValue", curve.k) ||
            !AccNumber(*value, "yParameterValue", curve.y1) || !AccNumber(*value, "zParameterValue", curve.x1) ||
            curve.k <= 0 || curve.y1 <= 0 || curve.y1 >= 1 || curve.x1 <= 0 ||
            !std::isfinite(std::exp(curve.k * curve.x1)) ||
            curve.y1 * std::exp(curve.k * curve.x1) <= 1) return false;
    } else {
        if (AccText(*value, "type") != "POINT_LOOKUP" || !AccNumber(*value, "scale", curve.scale) ||
            !AccNumber(*value, "shift", curve.shift) || curve.scale <= 0 ||
            !value->HasMember("points") || !(*value)["points"].IsArray()) return false;
        for (const auto& point : (*value)["points"].GetArray()) {
            double x, y;
            if (!AccNumber(point, "x", x) || !AccNumber(point, "y", y) || y < 0 ||
                (!curve.points.empty() && (x <= curve.points.back().first || y < curve.points.back().second))) return false;
            curve.points.emplace_back(x, y);
        }
        if (curve.points.size() < 2 || curve.points.front().first > 0 || curve.points.back().first < 1) return false;
    }
    return true;
}
bool AccMetadata(ScoreHistory& history, std::string& error, const HttpGetter& get) {
    if (history.accMetadataLoaded) return true;
    rapidjson::Document doc;
    if (!AccGet("/categories", doc, error, get)) return false;
    if (!doc.IsArray()) { error = "AccSaber categories are unavailable."; return false; }
    std::vector<AccSaberCategory> categories;
    std::unordered_set<std::string> ids, codes;
    for (const auto& row : doc.GetArray()) {
        AccSaberCategory c; c.id = AccText(row, "id"); c.code = AccText(row, "code");
        if (c.code == "overall") continue;
        if (c.id.empty() || NormalizeAccSaberCategory(c.code) != c.code || !ids.insert(c.id).second || !codes.insert(c.code).second ||
            !row.HasMember("countForOverall") || !row["countForOverall"].IsBool() ||
            !row.HasMember("scoreCurve") || !row.HasMember("weightCurve") ||
            !AccCurve(row["scoreCurve"], false, c.scoreCurve, error, get) ||
            !AccCurve(row["weightCurve"], true, c.weightCurve, error, get)) {
            if (error.empty()) error = "AccSaber category curves are incomplete or unsupported."; return false;
        }
        c.countForOverall = row["countForOverall"].GetBool(); categories.push_back(std::move(c));
    }
    if (categories.empty()) { error = "AccSaber has no supported ranking categories."; return false; }
    if (!AccGet("/modifiers", doc, error, get)) return false;
    if (!doc.IsArray()) { error = "AccSaber modifiers are unavailable."; return false; }
    std::unordered_map<std::string, std::string> modifiers;
    for (const auto& row : doc.GetArray()) {
        const auto id = AccText(row, "id"), code = AccText(row, "code");
        if (id.empty() || code.empty() || !modifiers.emplace(id, code).second) {
            error = "AccSaber returned invalid modifier definitions."; return false;
        }
    }
    history.accCategories = std::move(categories); history.accModifiers = std::move(modifiers);
    for (auto& chart : history.accCatalog) for (const auto& c : history.accCategories)
        if (chart.categoryId == c.id) chart.categoryCode = c.code;
    history.accMetadataLoaded = true; return true;
}
bool LoadAccHistory(const std::string& id, ScoreHistory& history, std::string& error,
                    const HttpGetter& get = HttpGet, bool pace = true, bool personal = false) {
    error.clear();
    if (!IsCanonicalAccountId(id)) { error = "Select an AccSaber account in Mod Settings."; return false; }
    if (history.loaded && history.playerId == id && history.system == RatingSystem::AccSaber &&
        (!personal || history.accMetadataLoaded)) return true;
    const bool importAttempts = history.importAttempts;
    ScoreHistory loaded; loaded.importAttempts = importAttempts;
    rapidjson::Document profile;
    if (!AccGet("/users/" + id, profile, error, get)) return false;
    const auto canonicalId = AccText(profile, "id");
    if (!IsCanonicalAccountId(canonicalId)) { error = "AccSaber returned an invalid account."; return false; }
    loaded.linkedBeatLeaderId = AccText(profile, "blId"); loaded.linkedScoreSaberId = AccText(profile, "ssId");
    if (personal && !AccMetadata(loaded, error, get)) return false;
    if (!AccCatalog(loaded, error, get, pace)) return false;
    std::unordered_map<std::string, MapEntry> catalog;
    for (const auto& chart : loaded.accCatalog) catalog.emplace(chart.leaderboardId, chart);
    std::unordered_set<std::string> postedCharts;
    if (!AccPages("/users/" + canonicalId + "/scores?sort=ap,desc", [&](const auto& row, auto& failure) {
        if (!row.IsObject() || !row.HasMember("active") || !row["active"].IsBool() ||
            !row.HasMember("partial") || !row["partial"].IsBool()) return false;
        if (!row["active"].GetBool() || row["partial"].GetBool()) return true;
        MapEntry e; e.leaderboardId = AccText(row, "mapDifficultyId");
        e.hash = Lower(AccText(row, "songHash")); e.songName = AccText(row, "songName");
        double postedScore;
        if (!AccIdentity(e, row) || e.leaderboardId.empty() || !AccNumber(row, "score", postedScore) || postedScore <= 0) return false;
        if (!postedCharts.insert(e.leaderboardId).second) { failure = "AccSaber repeated an active chart score."; return false; }
        const bool hasAp = AccNumber(row, "ap", e.pp), hasAccuracy = AccNumber(row, "accuracy", e.accuracy);
        const bool hasUnmodifiedScore = AccNumber(row, "scoreNoMods", e.scoreNoMods);
        loaded.syncScores.push_back(e);
        const auto found = catalog.find(e.leaderboardId);
        e.ranked = found != catalog.end();
        if (personal && e.ranked && (!hasAp || e.pp < 0 || !hasAccuracy || e.accuracy < 0 || e.accuracy > 1 ||
            !hasUnmodifiedScore || e.scoreNoMods <= 0 || !row.HasMember("modifierIds") || !row["modifierIds"].IsArray())) return false;
        if (e.ranked) {
            if (EntryKey(e) != EntryKey(found->second)) { failure = "AccSaber score/catalog identity mismatch."; return false; }
            auto normalized = found->second;
            normalized.pp = e.pp; normalized.accuracy = e.accuracy; normalized.scoreNoMods = e.scoreNoMods;
            e = std::move(normalized);
        }
        if (e.songName.empty()) e.songName = e.hash;
        ReadTimestamp(row, "timeSet", e.timepost);
        if (row.HasMember("modifierIds") && row["modifierIds"].IsArray()) for (const auto& modifier : row["modifierIds"].GetArray()) {
            if (!modifier.IsString()) return false;
            const auto it = loaded.accModifiers.find(modifier.GetString());
            if (personal && e.ranked && it == loaded.accModifiers.end()) { failure = "AccSaber returned an unknown score modifier."; return false; }
            if (it != loaded.accModifiers.end()) { if (!e.modifiers.empty()) e.modifiers += ","; e.modifiers += it->second; }
        }
        e.noFail = ("," + e.modifiers + ",").find(",NF,") != std::string::npos;
        e.baseAccuracy = e.maxScore > 0 ? e.scoreNoMods / e.maxScore : e.accuracy;
        loaded.scores.push_back(std::move(e)); return true;
    }, error, get, pace)) { if (error.empty()) error = "AccSaber returned an incomplete score identity or value."; return false; }
    loaded.loaded = loaded.syncComplete = true; loaded.playerId = id; loaded.system = RatingSystem::AccSaber;
    history = std::move(loaded); return true;
}
void LoadAccAttempts(ScoreHistory& history, const HttpGetter& get, bool pace) {
    if (history.attemptsLoaded) return;
    std::string warning;
    ReadAttemptHistory("accsaber", history.playerId, history.attempts, warning);
    history.attemptWarning = warning;
    if (history.importAttempts) for (const auto& pair : {
        std::pair{RatingSystem::BeatLeader, history.linkedBeatLeaderId},
        std::pair{RatingSystem::ScoreSaber, history.linkedScoreSaberId}}) {
        if (!IsCanonicalAccountId(pair.second)) continue;
        ScoreHistory linked;
        LoadAttempts(pair.second, pair.first, linked, get, pace);
        MergeAttempts(history.attempts, linked.attempts);
        if (!linked.attemptWarning.empty()) history.attemptWarning += " " + linked.attemptWarning;
    }
    // The model resolves chart identities to fresh complexity. Never persist
    // imported BeatLeader/ScoreSaber stars as if they were AccSaber ratings.
    for (auto& attempt : history.attempts) { attempt.stars = 0; attempt.ratingEpoch = 0; }
    history.attemptsLoaded = true;
}
bool FetchAccRecommendationsInternal(const std::string& id, const std::string& category,
    ListKind kind, int limit, double minClearRate, ScoreHistory& history,
    std::vector<MapEntry>& entries, std::string& error, const HttpGetter& get = HttpGet, bool pace = true) {
    entries.clear(); error.clear();
    if (!LoadAccHistory(id, history, error, get, pace, true)) return false;
    for (const auto& chart : history.accCatalog) if (chart.categoryCode.empty() || !chart.difficulties[0].complexity || chart.maxScore <= 0) {
        error = "AccSaber ranked map metadata is incomplete; previous playlist preserved."; return false;
    }
    const auto filter = NormalizeAccSaberCategory(category);
    bool evidence = false;
    for (const auto& c : history.accCategories) if ((filter == "overall" ? c.countForOverall : c.code == filter) &&
        HasAccSaberEvidence(history.scores, c.code)) evidence = true;
    if (!evidence) { error = "AccSaber needs eligible scores in " + AccSaberCategoryName(filter) +
        " before it can predict accuracy. Previous playlist preserved."; return false; }
    LoadAccAttempts(history, get, pace);
    entries = SelectAccSaberRecommendations(history.accCatalog, history.scores, history.accCategories,
        history.attempts, filter, kind, limit, minClearRate);
    return true;
}
