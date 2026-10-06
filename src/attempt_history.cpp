#include "attempt_history.hpp"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <stdexcept>

#ifndef RANKEDTOOLS_ATTEMPT_DIRECTORY
#define RANKEDTOOLS_ATTEMPT_DIRECTORY "/sdcard/ModData/com.beatgames.beatsaber/Mods/RankedPractice/attempts"
#endif
namespace rankedpractice {
namespace {
std::mutex attemptFileMutex;
constexpr size_t kMaximumStoredAttempts = 20000;
std::string AttemptLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}
bool ValidAttemptIdentity(const Attempt& a) {
    return !a.id.empty() && (a.source == "local" || a.source == "beatleader" || a.source == "scoresaber") &&
        a.hash.size() == 40 && std::all_of(a.hash.begin(), a.hash.end(), [](unsigned char c) { return std::isxdigit(c); }) &&
        !a.difficulty.empty() && std::isfinite(a.timestamp) && a.timestamp > 0 &&
        std::isfinite(a.endTime) && a.endTime >= 0 && std::isfinite(a.stars) && a.stars >= 0;
}
std::filesystem::path AttemptPath(const std::string& service, const std::string& id) {
    if ((service != "beatleader" && service != "scoresaber") || id.empty() ||
        !std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isdigit(c); }))
        throw std::runtime_error("Invalid attempt-history player ID or service.");
    return std::filesystem::path(RANKEDTOOLS_ATTEMPT_DIRECTORY) / (service + "_" + id + ".json");
}
bool ReadAttemptsUnlocked(const std::filesystem::path& path, std::vector<Attempt>& result, std::string& error) {
    result.clear();
    if (!std::filesystem::exists(path)) return true;
    std::ifstream file(path);
    if (!file) { error = "Could not read attempt history."; return false; }
    std::string text{std::istreambuf_iterator<char>(file), {}};
    rapidjson::Document doc;
    doc.Parse(text.c_str());
    if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("version") ||
        !doc["version"].IsInt() || doc["version"].GetInt() != 1 || !doc.HasMember("attempts") || !doc["attempts"].IsArray()) {
        error = "Attempt history is damaged or has an unsupported version; preserved the original file.";
        return false;
    }
    for (const auto& row : doc["attempts"].GetArray()) {
        if (!row.IsObject()) continue;
        Attempt a;
        auto str = [&](const char* key, std::string& out) { if (row.HasMember(key) && row[key].IsString()) out = row[key].GetString(); };
        auto num = [&](const char* key, double& out) { if (row.HasMember(key) && row[key].IsNumber()) out = row[key].GetDouble(); };
        str("id", a.id); str("source", a.source); str("hash", a.hash); str("characteristic", a.characteristic);
        str("localId", a.localId);
        str("difficulty", a.difficulty); str("modifiers", a.modifiers);
        num("timestamp", a.timestamp); num("endTime", a.endTime); num("stars", a.stars);
        num("ratingChecked", a.ratingChecked);
        if (row.HasMember("outcome") && row["outcome"].IsInt() && row["outcome"].GetInt() >= 0 && row["outcome"].GetInt() <= 5)
            a.outcome = static_cast<AttemptOutcome>(row["outcome"].GetInt());
        if (row.HasMember("normal") && row["normal"].IsBool()) a.normal = row["normal"].GetBool();
        if (row.HasMember("practice") && row["practice"].IsBool()) a.practice = row["practice"].GetBool();
        if (ValidAttemptIdentity(a)) result.push_back(std::move(a));
    }
    return true;
}
}
std::string AttemptChartKey(const Attempt& a) {
    return AttemptLower(a.hash) + "|" + AttemptLower(a.characteristic) + "|" + AttemptLower(a.difficulty);
}
bool IsTrainingAttempt(const Attempt& a) {
    return ValidAttemptIdentity(a) && a.normal && !a.practice && a.modifiers.empty() &&
        AttemptLower(a.characteristic) == "standard" && a.stars > 0 &&
        (a.outcome == AttemptOutcome::Clear || a.outcome == AttemptOutcome::Fail);
}
void MergeAttempts(std::vector<Attempt>& history, const std::vector<Attempt>& incoming) {
    // Exact provider IDs are authoritative. Cross-source matching is deliberately
    // narrow, and each existing event can match only once during this merge.
    std::unordered_set<size_t> matched;
    std::unordered_map<std::string, size_t> ids;
    std::unordered_map<std::string, std::vector<size_t>> buckets;
    auto eventKey = [](const Attempt& a) {
        return AttemptChartKey(a) + "|" + std::to_string(static_cast<int>(a.outcome)) + "|" +
            a.modifiers + "|" + (a.normal ? "1" : "0") + (a.practice ? "1" : "0");
    };
    for (size_t i = 0; i < history.size(); ++i) {
        ids[history[i].source + "|" + history[i].id] = i;
        if (!history[i].localId.empty()) ids["local|" + history[i].localId] = i;
        buckets[eventKey(history[i]) + (history[i].source == "local" ? "|local" : "|remote")].push_back(i);
    }
    for (const auto& a : incoming) {
        if (!ValidAttemptIdentity(a)) continue;
        size_t found = history.size();
        if (const auto id = ids.find(a.source + "|" + a.id); id != ids.end()) found = id->second;
        if (found == history.size()) {
            double closest = 30.01;
            for (const auto i : buckets[eventKey(a) + (a.source == "local" ? "|remote" : "|local")]) {
                const auto& b = history[i];
                if (matched.contains(i) || (a.source == "local") == (b.source == "local") ||
                    a.outcome != b.outcome || a.practice != b.practice || a.normal != b.normal ||
                    a.modifiers != b.modifiers || AttemptChartKey(a) != AttemptChartKey(b) ||
                    std::abs(a.endTime - b.endTime) > 1.0) continue;
                const double distance = std::abs(a.timestamp - b.timestamp);
                if (distance <= 30.0 && distance < closest) { found = i; closest = distance; }
            }
        }
        if (found == history.size()) {
            history.push_back(a);
            ids[a.source + "|" + a.id] = found;
            if (!a.localId.empty()) ids["local|" + a.localId] = found;
            buckets[eventKey(a) + (a.source == "local" ? "|local" : "|remote")].push_back(found);
        }
        else {
            const bool crossSource = (a.source == "local") != (history[found].source == "local");
            if (crossSource) matched.insert(found);
            const double stars = a.stars > 0 ? a.stars : history[found].stars;
            const double ratingChecked = std::max(a.ratingChecked, history[found].ratingChecked);
            const std::string localId = a.source == "local" ? a.id :
                (history[found].source == "local" ? history[found].id : history[found].localId);
            if (a.source != "local" || history[found].source == "local") {
                ids.erase(history[found].source + "|" + history[found].id);
                history[found] = a;
                ids[a.source + "|" + a.id] = found;
                if (crossSource) buckets[eventKey(a) + "|remote"].push_back(found);
            }
            history[found].stars = stars;
            history[found].ratingChecked = ratingChecked;
            history[found].localId = localId;
            if (!localId.empty()) ids["local|" + localId] = found;
        }
    }
    std::stable_sort(history.begin(), history.end(), [](const auto& a, const auto& b) { return a.timestamp > b.timestamp; });
    if (history.size() > kMaximumStoredAttempts) history.resize(kMaximumStoredAttempts);
}
bool ReadAttemptHistory(const std::string& service, const std::string& id, std::vector<Attempt>& attempts, std::string& error) {
    std::lock_guard lock(attemptFileMutex);
    try { return ReadAttemptsUnlocked(AttemptPath(service, id), attempts, error); }
    catch (const std::exception& ex) { error = ex.what(); return false; }
}
bool SaveAttemptHistory(const std::string& service, const std::string& id, const std::vector<Attempt>& incoming,
                        std::vector<Attempt>& merged, std::string& error) {
    std::lock_guard lock(attemptFileMutex);
    try {
        const auto path = AttemptPath(service, id);
        if (!ReadAttemptsUnlocked(path, merged, error)) return false;
        MergeAttempts(merged, incoming);
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        writer.StartObject(); writer.Key("version"); writer.Int(1); writer.Key("attempts"); writer.StartArray();
        for (const auto& a : merged) {
            writer.StartObject();
            auto str = [&](const char* key, const std::string& value) { writer.Key(key); writer.String(value.c_str()); };
            auto num = [&](const char* key, double value) { writer.Key(key); writer.Double(value); };
            str("id", a.id); str("source", a.source); str("hash", a.hash); str("characteristic", a.characteristic);
            str("localId", a.localId);
            str("difficulty", a.difficulty); str("modifiers", a.modifiers);
            num("timestamp", a.timestamp); num("endTime", a.endTime); num("stars", a.stars);
            num("ratingChecked", a.ratingChecked);
            writer.Key("outcome"); writer.Int(static_cast<int>(a.outcome));
            writer.Key("normal"); writer.Bool(a.normal); writer.Key("practice"); writer.Bool(a.practice); writer.EndObject();
        }
        writer.EndArray(); writer.EndObject();
        std::filesystem::create_directories(path.parent_path());
        const auto temporary = path.string() + ".tmp";
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(buffer.GetString(), buffer.GetSize()); file.flush();
        if (!file) { error = "Could not save attempt history."; return false; }
        file.close();
        std::filesystem::rename(temporary, path);
        return true;
    } catch (const std::exception& ex) { error = ex.what(); return false; }
}
} // namespace rankedpractice
