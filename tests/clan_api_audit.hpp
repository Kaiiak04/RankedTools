// Optional public-response audit. Network collection is separate; the production
// HTTP boundary reads a URL-to-file manifest, and lists missing requests for capture.
int AuditClanProfile(const std::string& id, const std::filesystem::path& directory, bool benchmark = false, bool paced = false) {
    std::unordered_map<std::string, std::string> files;
    std::ifstream manifest(directory / "manifest.tsv");
    std::string line;
    while (std::getline(manifest, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto separator = line.find('\t');
        if (separator != std::string::npos) files[line.substr(separator+1)] = line.substr(0,separator);
    }
    std::ofstream missing(directory / "missing.txt", std::ios::trunc);
    std::unordered_set<std::string> requested;
    std::atomic<std::size_t> requests{0}, bytes{0};
    std::atomic<bool> timing{false};
    std::mutex missingMutex;
    auto read = [&](const std::string& url, std::string& body) {
        const auto found = files.find(url);
        if (found == files.end()) return false;
        std::ifstream file(directory / found->second,std::ios::binary);
        if (!file) return false;
        body.assign(std::istreambuf_iterator<char>(file),{}); return true;
    };
    // Repage the old 100-row captures without network access. This changes only
    // transport packaging; chart and contribution data come from the same snapshot.
    auto repage = [&](const std::string& url, std::string& body) {
        const auto countPos = url.find("count=500"), pagePos = url.find("page=");
        if (countPos == std::string::npos || pagePos == std::string::npos) return false;
        const int page = std::stoi(url.substr(pagePos+5));
        auto oldUrl = url; oldUrl.replace(countPos,9,"count=100");
        rapidjson::Document merged; merged.SetObject(); auto& allocator = merged.GetAllocator();
        rapidjson::Value rows(rapidjson::kArrayType);
        int total = -1;
        for(int i=0;i<5;++i) {
            const int oldPage = (page-1)*5+i+1;
            auto sourceUrl = oldUrl; sourceUrl.replace(pagePos+5,std::string::npos,std::to_string(oldPage));
            std::string source;
            if (!read(sourceUrl,source)) return false;
            rapidjson::Document doc; doc.Parse(source.c_str());
            if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("data") || !doc["data"].IsArray() ||
                !doc.HasMember("metadata") || !doc["metadata"].HasMember("total")) return false;
            total = doc["metadata"]["total"].GetInt();
            if (i == 0 && doc.HasMember("container")) merged.AddMember("container",rapidjson::Value(doc["container"],allocator),allocator);
            for(const auto& row : doc["data"].GetArray()) rows.PushBack(rapidjson::Value(row,allocator),allocator);
            if ((page-1)*500+static_cast<int>(rows.Size()) >= total) break;
        }
        rapidjson::Value metadata(rapidjson::kObjectType);
        metadata.AddMember("total",total,allocator); metadata.AddMember("page",page,allocator); metadata.AddMember("itemsPerPage",500,allocator);
        merged.AddMember("metadata",metadata,allocator); merged.AddMember("data",rows,allocator);
        rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> writer(buffer); merged.Accept(writer);
        body.assign(buffer.GetString(),buffer.GetSize()); return true;
    };
    HttpGetter get = [&](const std::string& url, std::string& body, std::string& error) {
        ++requests;
        if (paced && timing.load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (read(url,body) || repage(url,body)) { bytes += body.size(); return true; }
        std::lock_guard<std::mutex> lock(missingMutex);
        if (requested.insert(url).second) missing << url << '\n';
        error = "Public response not captured yet."; return false;
    };
    HttpGetter icon = [](const std::string&, std::string&, std::string&) { return false; };
    ScoreHistory history; history.importAttempts = false;
    std::string error;
    std::vector<ClanPlaylistRecommendations> lists;
    // Audit PB-based behavior without importing or changing persistent attempt history.
    if (!LoadScoreHistory(id, RatingSystem::BeatLeader, history, error, get, false, false)) {
        std::cout << "PENDING: " << error << '\n'; return 2;
    }
    ClanScreenCache cache(false);
    std::vector<MapEntry> coldEntries;
    for(int pass=0;pass<(benchmark?2:1);++pass) {
        requests = 0; bytes = 0;
        const auto started = std::chrono::steady_clock::now();
        timing.store(true);
        if (!FetchClanToConquerInternal(id,false,16,lists,error,get,icon,.5,&history,paced,benchmark?&cache:nullptr)) {
            std::cout << "PENDING: " << error << '\n'; return 2;
        }
        if (benchmark) {
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
            std::cout << "BENCH " << (pass ? "warm" : "cold") << ": " << requests << " requests, " << bytes << " response bytes, "
                      << seconds << (paced ? " replay seconds (simulated 200ms/request; production pacing/workers and fixture I/O included; no network)\n"
                                           : " replay seconds (fixture I/O/repaging included; network/pacing excluded)\n");
            if (!pass && !lists.empty()) coldEntries = lists[0].entries;
            if (pass && !lists.empty()) {
                if (coldEntries.size() != lists[0].entries.size()) throw std::runtime_error("Warm cache changed recommendation count.");
                for(std::size_t i=0;i<coldEntries.size();++i)
                    if (coldEntries[i].leaderboardId != lists[0].entries[i].leaderboardId ||
                        std::abs(coldEntries[i].clanImpact->gain-lists[0].entries[i].clanImpact->gain) > .02)
                        throw std::runtime_error("Warm cache changed ranking or contribution.");
            }
        }
    }
    bool complete = true;
    std::cout << "PROFILE " << id << ": " << history.scores.size() << " PB rows\n";
    for (const auto& list : lists) {
        std::cout << "CLAN " << list.clanTag << ": " << list.entries.size() << " recommendations; checked "
                  << list.checkedCharts << '/' << list.eligibleCharts << "; failures " << list.failedCharts << '\n';
        if (!list.error.empty()) { std::cout << "PENDING: " << list.error << '\n'; complete = false; }
        for (const auto& entry : list.entries) {
            const auto& impact = *entry.clanImpact;
            std::cout << std::fixed << std::setprecision(2) << (impact.captures ? "CAPTURE " : "PROGRESS ")
                << entry.songName << " | " << entry.leaderboardId << " | " << entry.stars << " stars | "
                << "PB " << entry.currentPp << " -> " << entry.predictedPp << " | clan +" << impact.gain
                << " / gap " << impact.gap << " | clear " << entry.clearChance << '\n';
            if (entry.predictedPp <= entry.currentPp || impact.gain <= 0 ||
                (!impact.captures && impact.gain < 1) || entry.clearChance < .5)
                throw std::runtime_error("Audit produced an ineligible clan suggestion.");
        }
    }
    return complete && requested.empty() ? 0 : 2;
}
