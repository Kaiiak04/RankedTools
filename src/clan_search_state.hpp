// Private implementation, included after ClanCursorPath and ReadClanNumber.
constexpr std::size_t kMaxClanSearchEvidence = 4096;
bool ValidClanEvidence(const ClanSearchEvidence& e) {
    return !e.leaderboardId.empty() && e.leaderboardId.size() <= 256 &&
        std::isfinite(e.screenClanPp) && e.screenClanPp >= 0 &&
        std::isfinite(e.screenRivalPp) && e.screenRivalPp >= -1 &&
        std::isfinite(e.currentPp) && e.currentPp >= 0 && std::isfinite(e.predictedPp) && e.predictedPp > 0 &&
        std::isfinite(e.predictedAccuracy) && e.predictedAccuracy >= 0 && e.predictedAccuracy <= 1 &&
        std::isfinite(e.baselinePp) && e.baselinePp >= 0 && std::isfinite(e.rivalPp) && e.rivalPp >= 0 &&
        std::isfinite(e.gain);
}
ClanSearchState ReadClanSearchState(const std::string& id, const std::string& tag, std::string& warning) {
    try {
        const auto path = ClanCursorPath(id,tag);
        if (!std::filesystem::exists(path)) return {};
        if (std::filesystem::file_size(path) > 2*1024*1024) throw std::runtime_error("Oversized clan search history.");
        std::ifstream file(path);
        const std::string body{std::istreambuf_iterator<char>(file),{}};
        rapidjson::Document doc; doc.Parse(body.c_str());
        if (!file || doc.HasParseError() || !doc.IsObject() || !doc.HasMember("version") || !doc["version"].IsInt() ||
            !doc.HasMember("cursor") || !doc["cursor"].IsUint64()) throw std::runtime_error("Invalid clan search history.");
        ClanSearchState state; state.cursor=doc["cursor"].GetUint64();
        if (doc["version"].GetInt()==1) return state; // Preserve the old exploration position.
        if (doc["version"].GetInt()!=2 || !doc.HasMember("epoch") || !doc["epoch"].IsInt() ||
            doc["epoch"].GetInt()!=kBeatLeaderAttemptRatingEpoch || !doc.HasMember("primaryClanId") ||
            !doc["primaryClanId"].IsInt() || doc["primaryClanId"].GetInt()<0 ||
            !doc.HasMember("recommendations") || !doc["recommendations"].IsArray() || doc["recommendations"].Size()>256 ||
            !doc.HasMember("evidence") || !doc["evidence"].IsArray() || doc["evidence"].Size()>kMaxClanSearchEvidence)
            throw std::runtime_error("Incompatible clan search history.");
        state.primaryClanId=doc["primaryClanId"].GetInt();
        std::unordered_set<std::string> seen;
        for (const auto& value:doc["recommendations"].GetArray()) {
            if (!value.IsString() || !value.GetStringLength() || value.GetStringLength()>256 || !seen.insert(value.GetString()).second)
                throw std::runtime_error("Invalid remembered recommendation.");
            state.recommendations.push_back(value.GetString());
        }
        seen.clear();
        for (const auto& row:doc["evidence"].GetArray()) {
            ClanSearchEvidence e;
            if (!ReadString(row,"id",e.leaderboardId) || !ReadClanNumber(row,"screenClanPp",e.screenClanPp) ||
                !ReadClanNumber(row,"screenRivalPp",e.screenRivalPp) || !ReadClanNumber(row,"currentPp",e.currentPp) ||
                !ReadClanNumber(row,"predictedPp",e.predictedPp) || !ReadClanNumber(row,"predictedAccuracy",e.predictedAccuracy) ||
                !ReadClanNumber(row,"baselinePp",e.baselinePp) || !ReadClanNumber(row,"rivalPp",e.rivalPp) ||
                !ReadClanNumber(row,"gain",e.gain) || !row.HasMember("useful") || !row["useful"].IsBool() ||
                !ValidClanEvidence(e) || !seen.insert(e.leaderboardId).second)
                throw std::runtime_error("Invalid remembered clan impact.");
            e.useful=row["useful"].GetBool(); state.evidence.push_back(std::move(e));
        }
        return state;
    } catch (const std::exception&) {
        warning="Clan search history unavailable; opportunity learning restarted.";
        return {};
    }
}
bool PersistClanSearchState(const std::string& id, const std::string& tag, const ClanSearchState& state, std::string& error) {
    try {
        if (state.primaryClanId<0 || state.recommendations.size()>256 || state.evidence.size()>kMaxClanSearchEvidence)
            throw std::runtime_error("Invalid clan search history size.");
        rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
        w.StartObject(); w.Key("version"); w.Int(2); w.Key("epoch"); w.Int(kBeatLeaderAttemptRatingEpoch);
        w.Key("cursor"); w.Uint64(state.cursor); w.Key("primaryClanId"); w.Int(state.primaryClanId);
        std::unordered_set<std::string> seen;
        w.Key("recommendations"); w.StartArray();
        for (const auto& lb:state.recommendations) {
            if (lb.empty() || lb.size()>256 || !seen.insert(lb).second) throw std::runtime_error("Invalid remembered recommendation.");
            w.String(lb.c_str(),lb.size());
        }
        w.EndArray(); w.Key("evidence"); w.StartArray(); seen.clear();
        for (const auto& e:state.evidence) {
            if (!ValidClanEvidence(e) || !seen.insert(e.leaderboardId).second) throw std::runtime_error("Invalid remembered clan impact.");
            w.StartObject(); w.Key("id"); w.String(e.leaderboardId.c_str(),e.leaderboardId.size());
            auto number=[&](const char* key,double value) { w.Key(key); w.Double(value); };
            number("screenClanPp",e.screenClanPp); number("screenRivalPp",e.screenRivalPp);
            number("currentPp",e.currentPp); number("predictedPp",e.predictedPp); number("predictedAccuracy",e.predictedAccuracy);
            number("baselinePp",e.baselinePp); number("rivalPp",e.rivalPp); number("gain",e.gain);
            w.Key("useful"); w.Bool(e.useful); w.EndObject();
        }
        w.EndArray(); w.EndObject();
        if (buffer.GetSize()>2*1024*1024) throw std::runtime_error("Oversized clan search history.");
        const auto path=ClanCursorPath(id,tag);
        std::filesystem::create_directories(path.parent_path());
        auto temp=path; temp+=".tmp";
        { std::ofstream file(temp,std::ios::trunc); file.write(buffer.GetString(),buffer.GetSize());
          file.flush(); if (!file) throw std::runtime_error("Could not write clan search history."); }
        std::error_code ec; std::filesystem::rename(temp,path,ec);
        if (ec) { std::filesystem::remove(path,ec); ec.clear(); std::filesystem::rename(temp,path,ec); }
        if (ec) throw std::runtime_error("Could not save clan search history.");
        return true;
    } catch (const std::exception& e) { error=e.what(); return false; }
}
