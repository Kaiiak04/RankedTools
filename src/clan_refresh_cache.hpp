// Private implementation, included inside recommendation_api.cpp's namespace.
constexpr int kClanBulkPageSize = 500;
constexpr double kClanCatalogCacheSeconds = 600;
constexpr double kClanTotalsCacheSeconds = 60;
double ClanCacheNow() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}
struct ClanScreenCache {
    struct Entry { double savedAt; std::string body; };
    std::mutex mutex;
    std::unordered_map<std::string, Entry> entries;
    bool persistent;
    explicit ClanScreenCache(bool persistent = true) : persistent(persistent) {}
    static std::filesystem::path Path(const std::string& key) {
        if (key.empty() || !std::all_of(key.begin(), key.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; }))
            throw std::runtime_error("Invalid clan screening cache key.");
        return std::filesystem::path(RANKEDTOOLS_CLAN_CURSOR_DIRECTORY) / ("screen_" + key + ".json");
    }
    bool Get(const std::string& key, double ttl, double now, std::string& body) {
        std::lock_guard<std::mutex> lock(mutex);
        try {
            auto found = entries.find(key);
            if (found == entries.end() && persistent) {
                const auto path = Path(key);
                if (!std::filesystem::exists(path) || std::filesystem::file_size(path) > 16 * 1024 * 1024) return false;
                std::ifstream file(path);
                const std::string json{std::istreambuf_iterator<char>(file), {}};
                rapidjson::Document doc; doc.Parse(json.c_str());
                double savedAt = 0;
                if (!file || doc.HasParseError() || !doc.IsObject() || !doc.HasMember("version") || !doc["version"].IsInt() ||
                    doc["version"].GetInt() != 1 || !doc.HasMember("epoch") || !doc["epoch"].IsInt() ||
                    doc["epoch"].GetInt() != kBeatLeaderAttemptRatingEpoch || !ReadClanNumber(doc,"savedAt",savedAt) ||
                    !doc.HasMember("body") || !doc["body"].IsString()) return false;
                found = entries.emplace(key, Entry{savedAt,doc["body"].GetString()}).first;
            }
            if (found == entries.end() || now < found->second.savedAt || now - found->second.savedAt >= ttl) return false;
            body = found->second.body; return true;
        } catch (const std::exception&) { return false; }
    }
    bool Put(const std::string& key, const std::string& body, double now) {
        std::lock_guard<std::mutex> lock(mutex);
        if (body.empty()) return false;
        entries.insert_or_assign(key, Entry{now,body});
        if (!persistent) return true;
        try {
            const auto path = Path(key);
            std::filesystem::create_directories(path.parent_path());
            rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            writer.StartObject(); writer.Key("version"); writer.Int(1); writer.Key("epoch"); writer.Int(kBeatLeaderAttemptRatingEpoch);
            writer.Key("savedAt"); writer.Double(now); writer.Key("body"); writer.String(body.c_str(),body.size()); writer.EndObject();
            auto temp = path; temp += ".tmp";
            { std::ofstream file(temp,std::ios::binary | std::ios::trunc); file.write(buffer.GetString(),buffer.GetSize()); file.flush();
              if (!file) return false; }
            std::error_code ec; std::filesystem::rename(temp,path,ec);
            if (ec) { std::filesystem::remove(path,ec); ec.clear(); std::filesystem::rename(temp,path,ec); }
            return !ec;
        } catch (const std::exception&) { return false; }
    }
};
std::string CompactClanCatalog(const std::vector<MapEntry>& charts) {
    rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
    auto str = [&](const char* key, const std::string& value) { w.Key(key); w.String(value.c_str(),value.size()); };
    auto num = [&](const char* key, double value) { w.Key(key); w.Double(value); };
    w.StartObject(); w.Key("data"); w.StartArray();
    for (const auto& chart : charts) {
        for (const auto& d : chart.difficulties) {
            if (!std::isfinite(d.stars) || !std::isfinite(d.accRating) || !std::isfinite(d.passRating) ||
                !std::isfinite(d.techRating) || !std::isfinite(d.predictedAcc) || !std::isfinite(d.sliderPercentage) ||
                !std::isfinite(d.notes) || (d.noteSpan && !std::isfinite(*d.noteSpan))) return {};
        }
        w.StartObject(); str("hash",chart.hash); str("name",chart.songName); str("subName",chart.songSubName);
        str("author",chart.songAuthorName); str("mapper",chart.levelAuthorName); str("leaderboardId",chart.leaderboardId);
        w.Key("difficulties"); w.StartArray();
        for (const auto& d : chart.difficulties) {
            w.StartObject(); str("modeName",d.characteristic); str("difficultyName",d.name);
            num("stars",d.stars); num("accRating",d.accRating); num("passRating",d.passRating); num("techRating",d.techRating);
            num("predictedAcc",d.predictedAcc); num("sliderPercentage",d.sliderPercentage); num("notes",d.notes);
            if (d.noteSpan) num("noteSpan",*d.noteSpan);
            w.EndObject();
        }
        w.EndArray(); w.EndObject();
    }
    w.EndArray(); w.EndObject(); return {buffer.GetString(),buffer.GetSize()};
}
std::string CompactClanTotals(const std::vector<std::pair<std::string,ClanTotal>>& totals,
                              const std::string& tag, const std::string& icon) {
    rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
    w.StartObject(); w.Key("container"); w.StartObject(); w.Key("tag"); w.String(tag.c_str()); w.Key("icon"); w.String(icon.c_str()); w.EndObject();
    w.Key("data"); w.StartArray();
    for (const auto& [id,total] : totals) {
        w.StartObject(); w.Key("leaderboardId"); w.String(id.c_str()); w.Key("pp"); w.Double(total.pp); w.Key("rank"); w.Int(total.rank); w.EndObject();
    }
    w.EndArray(); w.EndObject(); return {buffer.GetString(),buffer.GetSize()};
}

// Upstream enables endpoint rate limiting: its default PathCounterKeyBuilder
// separates map IDs but not query pages. Keep repeated endpoint calls below
// 50/10s and retain a 90ms start gap across independent maps, with two workers.
class ClanRequestGate {
    std::mutex mutex;
    std::chrono::steady_clock::time_point next{};
    std::unordered_map<std::string,std::chrono::steady_clock::time_point> endpoints;
public:
    std::atomic<bool> limited{false};
    bool Acquire(bool pace, const std::string& url = {}) {
        std::unique_lock<std::mutex> lock(mutex);
        if (limited.load()) return false;
        const auto endpoint = url.substr(0,url.find('?'));
        auto& endpointNext = endpoints[endpoint];
        if (pace) std::this_thread::sleep_until(std::max(next,endpointNext));
        if (limited.load()) return false;
        const auto now = std::chrono::steady_clock::now();
        next = now + std::chrono::milliseconds(90);
        endpointNext = now + std::chrono::milliseconds(225);
        return true;
    }
};

// Start at most one extra worker, always join, and retain main-thread execution
// if creating a thread is unavailable on the device. Each index has one owner.
template<class Work>
void RunClanJobs(std::size_t count, bool parallel, Work work) {
    std::atomic<std::size_t> next{0};
    auto run = [&] { for (;;) { const auto i = next.fetch_add(1); if (i >= count) return; work(i); } };
    std::future<void> second;
    if (parallel && count > 1) {
        try { second = std::async(std::launch::async,run); }
        catch (const std::system_error&) {}
    }
    try { run(); }
    catch (...) { if (second.valid()) second.wait(); throw; }
    if (second.valid()) second.get();
}
