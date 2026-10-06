#include "recommendation_api.hpp"
#include "playlist_image.hpp"
#include "account.hpp"

#include "libcurl/shared/curl.h"
#include <rapidjson/document.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <limits>
#include <ctime>
#include <iomanip>
#include <string_view>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <unordered_map>

namespace rankedpractice {
namespace {

constexpr const char* kCaBundlePath =
    "/sdcard/ModData/com.beatgames.beatsaber/Mods/RankedPractice/cacert.pem";
constexpr double kDefaultTargetStars = 7.0;
constexpr double kTargetWindowStars = 1.50;

struct Context {
    std::string hash;
    std::string songName;
    std::string songSubName;
    std::string songAuthorName;
    std::string levelAuthorName;
    std::string leaderboardId;
    std::string characteristic;
    std::string difficulty;
    double stars{0.0};
    double modifiedStars{0.0};
    double accRating{0.0};
    double passRating{0.0};
    double techRating{0.0};
    double pp{0.0};
    double accuracy{0.0};
    double timepost{0.0};
    double scoreValue{0.0};
    double baseScoreValue{0.0};
    double maxScore{0.0};
    bool applicable{true};
    bool noFail{false};
    bool ranked{true};
    std::vector<std::string> modifiers;
};

struct CurlResponse {
    std::string& body;
    size_t limit;
};

size_t CurlWrite(char* data, size_t size, size_t count, void* userData) {
    auto& response = *static_cast<CurlResponse*>(userData);
    if (count && size > std::numeric_limits<size_t>::max() / count) return 0;
    const auto bytes = size * count;
    if (bytes > response.limit - response.body.size()) return 0;
    response.body.append(data, bytes);
    return bytes;
}

bool HttpGetInternal(const std::string& url, std::string& body, std::string& error,
                     size_t limit, long timeout) {
    static const bool curlReady = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    if (!curlReady) {
        error = "Could not initialize the HTTPS client.";
        return false;
    }
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "Could not create an HTTPS request.";
        return false;
    }
    body.clear();
    CurlResponse response{body, limit};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, std::min(10L, timeout));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "RankedPractice/0.1 (Beat Saber Quest mod)");
    // The Android libcurl package used by Quest does not ship with a default
    // CA bundle. The QMOD copies cacert.pem to this path at install time.
    if (std::filesystem::exists(kCaBundlePath)) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, kCaBundlePath);
    } else {
        // Keep a useful fallback for devices whose system CA directory is
        // readable by OpenSSL.
        curl_easy_setopt(curl, CURLOPT_CAPATH, "/system/etc/security/cacerts");
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    const auto result = curl_easy_perform(curl);
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK) {
        error = std::string("Request failed: ") + curl_easy_strerror(result);
        return false;
    }
    if (responseCode < 200 || responseCode >= 300) {
        error = "Leaderboard API returned HTTP " + std::to_string(responseCode) + ".";
        return false;
    }
    return true;
}

bool HttpGet(const std::string& url, std::string& body, std::string& error) {
    return HttpGetInternal(url, body, error, std::numeric_limits<size_t>::max(), 18L);
}

constexpr size_t kMaxClanIconBytes = 4 * 1024 * 1024;
bool HttpGetIcon(const std::string& url, std::string& body, std::string& error) {
    return HttpGetInternal(url, body, error, kMaxClanIconBytes, 8L);
}

std::string StringValue(const rapidjson::Value& value) {
    if (value.IsString()) return value.GetString();
    if (value.IsInt64()) return std::to_string(value.GetInt64());
    if (value.IsUint64()) return std::to_string(value.GetUint64());
    return {};
}

double NumberValue(const rapidjson::Value& value, double fallback) {
    if (value.IsNumber()) return value.GetDouble();
    if (value.IsString()) {
        try { return std::stod(value.GetString()); } catch (...) {}
    }
    return fallback;
}

bool ReadString(const rapidjson::Value& object, const char* key, std::string& output) {
    if (!object.IsObject() || !object.HasMember(key)) return false;
    const auto value = StringValue(object[key]);
    if (value.empty()) return false;
    output = value;
    return true;
}

bool ReadNumber(const rapidjson::Value& object, const char* key, double& output) {
    if (!object.IsObject() || !object.HasMember(key)) return false;
    const auto& value = object[key];
    if (!value.IsNumber() && !value.IsString()) return false;
    output = NumberValue(value, output);
    return true;
}

bool ReadBool(const rapidjson::Value& object, const char* key, bool& output) {
    if (!object.IsObject() || !object.HasMember(key) || !object[key].IsBool()) return false;
    output = object[key].GetBool();
    return true;
}

double TimestampValue(const rapidjson::Value& value) {
    if (value.IsNumber()) {
        const double timestamp = value.GetDouble();
        return std::isfinite(timestamp) && timestamp > 0.0 ? timestamp : 0.0;
    }
    if (!value.IsString()) return 0.0;
    const std::string text = value.GetString();
    try {
        size_t consumed = 0;
        const double timestamp = std::stod(text, &consumed);
        if (consumed == text.size() && std::isfinite(timestamp) && timestamp > 0.0) return timestamp;
    } catch (...) {}
    // ISO 8601 dates are UTC, independent of the headset's local timezone.
    if (text.size() < 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':') return 0.0;
    auto digits = [&text](size_t start, size_t count) -> int {
        int result = 0;
        for (size_t i = start; i < start + count; ++i) {
            if (i >= text.size() || text[i] < '0' || text[i] > '9') return -1;
            result = result * 10 + text[i] - '0';
        }
        return result;
    };
    const int year = digits(0, 4), month = digits(5, 2), day = digits(8, 2);
    const int hour = digits(11, 2), minute = digits(14, 2), second = digits(17, 2);
    const std::chrono::year_month_day date{std::chrono::year{year},
        std::chrono::month{static_cast<unsigned>(month)}, std::chrono::day{static_cast<unsigned>(day)}};
    if (year < 1970 || !date.ok() || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59) return 0.0;
    size_t zone = 19;
    double fraction = 0.0;
    if (text[zone] == '.') {
        const size_t start = ++zone;
        double place = 0.1;
        while (zone < text.size() && std::isdigit(static_cast<unsigned char>(text[zone]))) {
            fraction += (text[zone++] - '0') * place;
            place *= 0.1;
        }
        if (zone == start) return 0.0;
    }
    if (zone >= text.size()) return 0.0;
    int offsetSeconds = 0;
    if (text[zone] == 'Z' && zone + 1 == text.size()) {
        // Already UTC.
    } else if ((text[zone] == '+' || text[zone] == '-') && zone + 6 == text.size() && text[zone + 3] == ':') {
        const int offsetHours = digits(zone + 1, 2), offsetMinutes = digits(zone + 4, 2);
        if (offsetHours < 0 || offsetHours > 23 || offsetMinutes < 0 || offsetMinutes > 59) return 0.0;
        offsetSeconds = (offsetHours * 3600 + offsetMinutes * 60) * (text[zone] == '+' ? 1 : -1);
    } else return 0.0;
    return std::chrono::duration<double>(std::chrono::sys_days{date}.time_since_epoch()).count() +
           hour * 3600 + minute * 60 + second + fraction - offsetSeconds;
}

void ReadTimestamp(const rapidjson::Value& object, const char* key, double& timestamp) {
    if (!object.IsObject() || !object.HasMember(key)) return;
    const double parsed = TimestampValue(object[key]);
    if (parsed > 0.0) timestamp = parsed;
}

std::vector<std::string> ModifierTokens(const std::string& modifiers) {
    std::vector<std::string> result;
    std::string token;
    auto finish = [&] {
        if (!token.empty() && std::find(result.begin(), result.end(), token) == result.end()) result.push_back(token);
        token.clear();
    };
    for (unsigned char character : modifiers) {
        if (std::isalnum(character)) token.push_back(static_cast<char>(std::toupper(character)));
        else finish();
    }
    finish();
    return result;
}

void ReadNoFailModifier(const rapidjson::Value& object, Context& context) {
    if (!object.IsObject()) return;
    auto addModifiers = [&context](const std::string& text) {
        for (const auto& token : ModifierTokens(text)) {
            if (token == "NF") context.noFail = true;
            if (std::find(context.modifiers.begin(), context.modifiers.end(), token) == context.modifiers.end()) {
                context.modifiers.push_back(token);
            }
        }
    };
    for (const char* key : {"modifiers", "modifier", "mods"}) {
        if (!object.HasMember(key)) continue;
        const auto& value = object[key];
        if (value.IsString()) {
            addModifiers(value.GetString());
        } else if (value.IsArray()) {
            for (const auto& modifier : value.GetArray()) {
                if (modifier.IsString()) {
                    addModifiers(modifier.GetString());
                }
            }
        }
    }
    bool explicitNoFail = false;
    if (ReadBool(object, "noFail", explicitNoFail)) context.noFail = context.noFail || explicitNoFail;
    explicitNoFail = false;
    if (ReadBool(object, "noFailModifier", explicitNoFail)) context.noFail = context.noFail || explicitNoFail;
}

bool IsDigits(const std::string& value);
std::string Lower(std::string text);

std::string DifficultyFromNumber(const std::string& raw) {
    if (raw == "0") return "Easy";
    // ScoreSaber's public API uses the legacy enum values 1, 3, 5, 7, 9.
    // BeatLeader provides difficultyName in its map and score responses.
    if (raw == "1") return "Easy";
    if (raw == "2" || raw == "5") return "Hard";
    if (raw == "3") return "Normal";
    if (raw == "4" || raw == "9") return "ExpertPlus";
    if (raw == "7") return "Expert";
    return raw;
}

std::string NormalizeDifficulty(std::string raw) {
    if (raw.size() > 1 && raw.front() == '_') {
        const auto separator = raw.find('_', 1);
        raw = raw.substr(1, separator == std::string::npos ? std::string::npos : separator - 1);
    }
    if (raw == "Expert+") return "ExpertPlus";
    if (raw == "ExpertPlus") return raw;
    if (!raw.empty() && std::all_of(raw.begin(), raw.end(), [](unsigned char c) { return std::isdigit(c); })) {
        return DifficultyFromNumber(raw);
    }
    return raw;
}

std::string NormalizeCharacteristic(std::string raw) {
    if (raw == "SoloStandard") return "Standard";
    if (raw == "SoloOneSaber") return "OneSaber";
    if (raw == "SoloNoArrows") return "NoArrows";
    return raw;
}

void UpdateContext(const rapidjson::Value& object, Context& context) {
    ReadString(object, "songHash", context.hash);
    ReadString(object, "hash", context.hash);
    ReadString(object, "songName", context.songName);
    // BeatLeader and ScoreSaber place these fields under a nested `song` object
    // and use their compact API names rather than the playlist field names.
    if (!context.hash.empty()) {
        ReadString(object, "name", context.songName);
        ReadString(object, "subName", context.songSubName);
        ReadString(object, "author", context.songAuthorName);
        ReadString(object, "mapper", context.levelAuthorName);
    }
    ReadString(object, "songSubName", context.songSubName);
    ReadString(object, "songAuthorName", context.songAuthorName);
    ReadString(object, "levelAuthorName", context.levelAuthorName);
    ReadString(object, "leaderboardId", context.leaderboardId);

    std::string levelId;
    if (ReadString(object, "levelId", levelId) && levelId.rfind("custom_level_", 0) == 0) {
        context.hash = levelId.substr(13);
    }

    std::string field;
    if (ReadString(object, "modeName", field) || ReadString(object, "characteristic", field)) {
        context.characteristic = NormalizeCharacteristic(field);
    } else if (ReadString(object, "gameMode", field) && !IsDigits(field)) {
        context.characteristic = NormalizeCharacteristic(field);
    } else if (ReadString(object, "mode", field) &&
               (field == "Standard" || field == "OneSaber" || field == "90Degree" || field == "360Degree")) {
        context.characteristic = NormalizeCharacteristic(field);
    }

    if (ReadString(object, "difficultyRaw", field) || ReadString(object, "rawDifficulty", field) || ReadString(object, "difficultyName", field) ||
        ReadString(object, "diff", field)) {
        context.difficulty = NormalizeDifficulty(field);
    } else if (object.IsObject() && object.HasMember("difficulty") &&
               (object["difficulty"].IsString() || object["difficulty"].IsNumber())) {
        context.difficulty = NormalizeDifficulty(StringValue(object["difficulty"]));
    }

    ReadNumber(object, "stars", context.stars);
    ReadNumber(object, "starRating", context.stars);
    // BeatLeader stores the played score's modifier-adjusted star rating as
    // modifiedStars. Keep it separate so candidates retain ordinary stars.
    ReadNumber(object, "modifiedStars", context.modifiedStars);
    ReadNumber(object, "accRating", context.accRating);
    ReadNumber(object, "accuracyRating", context.accRating);
    ReadNumber(object, "passRating", context.passRating);
    ReadNumber(object, "techRating", context.techRating);
    ReadNumber(object, "pp", context.pp);
    ReadNumber(object, "accuracy", context.accuracy);
    ReadNumber(object, "acc", context.accuracy);
    ReadNumber(object, "accuracyPercent", context.accuracy);
    ReadNumber(object, "score", context.scoreValue);
    ReadNumber(object, "modifiedScore", context.scoreValue);
    ReadNumber(object, "baseScore", context.baseScoreValue);
    ReadNumber(object, "maxScore", context.maxScore);
    ReadTimestamp(object, "timepost", context.timepost);
    ReadTimestamp(object, "timeset", context.timepost);
    ReadTimestamp(object, "timeSet", context.timepost);
    if (context.timepost <= 0.0) ReadTimestamp(object, "createdAt", context.timepost);
    ReadBool(object, "applicable", context.applicable);
    ReadBool(object, "ranked", context.ranked);
    if (object.HasMember("status") && object["status"].IsInt() && object.HasMember("difficultyName")) {
        context.ranked = object["status"].GetInt() == 3;
    }
    std::string leaderboardStatus;
    if (ReadString(object, "leaderboardStatus", leaderboardStatus)) context.ranked = leaderboardStatus == "RANKED";
    ReadNoFailModifier(object, context);
    if (context.accuracy > 1.0) context.accuracy /= 100.0;
}

void AddIfComplete(const Context& context, std::vector<MapEntry>& entries, bool score = false) {
    if (!context.applicable || context.hash.empty() || context.songName.empty() ||
        context.difficulty.empty() || (!score && context.stars <= 0.0)) return;
    MapEntry entry;
    entry.hash = context.hash;
    entry.songName = context.songName;
    entry.songSubName = context.songSubName;
    entry.songAuthorName = context.songAuthorName;
    entry.levelAuthorName = context.levelAuthorName;
    entry.leaderboardId = context.leaderboardId;
    entry.stars = context.stars;
    entry.modifiedStars = context.modifiedStars;
    entry.pp = context.pp;
    entry.accuracy = context.accuracy;
    entry.baseAccuracy = context.baseScoreValue > 0.0 && context.maxScore > 0.0
        ? std::clamp(context.baseScoreValue / context.maxScore, 0.0, 1.0)
        : context.accuracy;
    entry.timepost = context.timepost;
    entry.noFail = context.noFail;
    entry.ranked = context.ranked;
    entry.difficulties.push_back({context.characteristic.empty() ? "Standard" : context.characteristic,
                                  context.difficulty, context.stars, context.accRating,
                                  context.passRating, context.techRating});
    entries.push_back(std::move(entry));
}

void DeriveModifiedStars(const rapidjson::Value& chart, Context& context) {
    if (context.modifiedStars > 0.0 || context.stars <= 0.0) return;
    double stars = context.stars;
    const bool hasRatings = chart.HasMember("modifiersRating") && chart["modifiersRating"].IsObject();
    if (hasRatings) {
        for (const auto& [token, key] : {std::pair{"SS", "ssStars"}, {"FS", "fsStars"}, {"SF", "sfStars"}}) {
            if (std::find(context.modifiers.begin(), context.modifiers.end(), token) != context.modifiers.end()) {
                ReadNumber(chart["modifiersRating"], key, stars);
                break;
            }
        }
    }
    double multiplier = 1.0;
    if (chart.HasMember("modifierValues") && chart["modifierValues"].IsObject()) {
        for (auto member = chart["modifierValues"].MemberBegin(); member != chart["modifierValues"].MemberEnd(); ++member) {
            const auto tokens = ModifierTokens(member->name.GetString());
            if (tokens.size() != 1) continue;
            const auto& token = tokens.front();
            // NF's -100% PP penalty says nothing about the attempted difficulty.
            if (token == "NF" || (hasRatings && (token == "SS" || token == "FS" || token == "SF"))) continue;
            if (std::find(context.modifiers.begin(), context.modifiers.end(), token) != context.modifiers.end()) {
                multiplier += NumberValue(member->value, 0.0);
            }
        }
    }
    context.modifiedStars = std::isfinite(stars * multiplier) ? std::max(0.0, stars * multiplier) : 0.0;
}

void VisitJson(const rapidjson::Value& value, const Context& parent, std::vector<MapEntry>& entries) {
    if (value.IsArray()) {
        for (const auto& row : value.GetArray()) VisitJson(row, parent, entries);
        return;
    }
    if (!value.IsObject()) return;
    for (const char* key : {"data", "playerScores", "leaderboards"}) {
        if (value.HasMember(key) && value[key].IsArray()) {
            Context collectionContext;
            // BeatLeader's hash lookup places the song beside its chart array.
            if (std::string_view(key) == "leaderboards" && value.HasMember("song") && value["song"].IsObject())
                UpdateContext(value["song"], collectionContext);
            VisitJson(value[key], collectionContext, entries);
            return;
        }
    }
    const auto& leaderboard = value.HasMember("leaderboard") && value["leaderboard"].IsObject()
        ? value["leaderboard"] : value;
    auto context = parent;
    UpdateContext(leaderboard, context);
    for (const char* key : {"song", "map", "realm"}) {
        if (leaderboard.HasMember(key) && leaderboard[key].IsObject()) UpdateContext(leaderboard[key], context);
    }
    if (context.leaderboardId.empty()) ReadString(leaderboard, "id", context.leaderboardId);
    // BeatLeader map rows contain multiple charts. Emit once per chart; nested
    // ratings, mapper profiles, and score improvements are never score rows.
    if (leaderboard.HasMember("difficulties") && leaderboard["difficulties"].IsArray()) {
        for (const auto& chart : leaderboard["difficulties"].GetArray()) {
            if (!chart.IsObject()) continue;
            auto chartContext = context;
            UpdateContext(chart, chartContext);
            if (chartContext.ranked) AddIfComplete(chartContext, entries);
        }
        return;
    }
    const rapidjson::Value* chart = nullptr;
    if (leaderboard.HasMember("difficulty") && leaderboard["difficulty"].IsObject()) {
        chart = &leaderboard["difficulty"];
        UpdateContext(*chart, context);
    }
    const bool score = value.HasMember("baseScore") ||
        (value.HasMember("score") && value["score"].IsObject());
    if (score) {
        const auto& scoreObject = value.HasMember("score") && value["score"].IsObject() ? value["score"] : value;
        UpdateContext(scoreObject, context);
        if (chart && (chart->HasMember("modifiersRating") || chart->HasMember("modifierValues"))) {
            DeriveModifiedStars(*chart, context);
        }
    }
    if (context.accuracy <= 0.0 && context.scoreValue > 0.0 && context.maxScore > 0.0) {
        context.accuracy = context.scoreValue / context.maxScore;
    }
    if (score || context.ranked) AddIfComplete(context, entries, score);
}

struct PageInfo {
    size_t rawRows{0};
    double total{-1.0};
    double totalPages{-1.0};
    double page{0.0};
};

bool ParseEntries(const std::string& json, std::vector<MapEntry>& entries, std::string& error,
                  PageInfo* pageInfo = nullptr) {
    rapidjson::Document document;
    document.Parse(json.c_str());
    if (document.HasParseError()) {
        error = "Leaderboard API returned invalid JSON.";
        return false;
    }
    if (pageInfo) {
        *pageInfo = {};
        const rapidjson::Value* rows = document.IsArray() ? &document : nullptr;
        if (document.IsObject()) {
            for (const char* key : {"data", "playerScores", "leaderboards"}) {
                if (document.HasMember(key) && document[key].IsArray()) { rows = &document[key]; break; }
            }
            if (document.HasMember("metadata") && document["metadata"].IsObject()) {
                const auto& metadata = document["metadata"];
                ReadNumber(metadata, "total", pageInfo->total);
                ReadNumber(metadata, "totalItems", pageInfo->total);
                ReadNumber(metadata, "totalPages", pageInfo->totalPages);
                ReadNumber(metadata, "page", pageInfo->page);
            }
        }
        if (!rows) { error = "Leaderboard API returned an unexpected page format."; return false; }
        pageInfo->rawRows = rows->Size();
    }
    VisitJson(document, Context{}, entries);
    return true;
}

using HttpGetter = std::function<bool(const std::string&, std::string&, std::string&)>;

bool FindAccountsInternal(AccountService service, const std::string& input, int page,
                           AccountPage& result, std::string& error, const HttpGetter& get) {
    result = {};
    AccountQuery query;
    if (!ParseAccountInput(service, input, query, error)) return false;
    std::string body;
    if (!get(AccountRequestUrl(service, query, page), body, error)) return false;
    if (body.size() > 2 * 1024 * 1024) { error = "The account response was too large."; return false; }
    return ParseAccountPage(service, query, page, body, result, error);
}

template<class Entry, class Parser>
bool FetchPages(const std::function<std::string(int)>& urlForPage,
                std::vector<Entry>& entries, std::string& error, Parser parse,
                const HttpGetter& get, bool pace, int pageLimit = std::numeric_limits<int>::max()) {
    size_t visitedRows = 0;
    std::string previousBody;
    for (int page = 1; ; ++page) {
        std::string body;
        if (!get(urlForPage(page), body, error)) return false;
        if (page > 1 && body == previousBody) {
            error = "Leaderboard API repeated a page; refresh stopped to avoid an incomplete playlist.";
            return false;
        }
        PageInfo info;
        if (!parse(body, entries, error, &info)) return false;
        if (info.page != 0.0 && info.page != page) {
            error = "Leaderboard API returned the wrong page number.";
            return false;
        }
        if (info.rawRows == 0) {
            if (info.total >= 0.0 && visitedRows < info.total) {
                error = "Leaderboard API ended before all reported rows were returned.";
                return false;
            }
            return true;
        }
        visitedRows += info.rawRows;
        if (info.total >= 0.0 && visitedRows >= info.total) return true;
        if (info.totalPages >= 0.0 && page >= info.totalPages) {
            if (info.total >= 0.0 && visitedRows < info.total) {
                error = "Leaderboard API page count ended before all reported rows were returned.";
                return false;
            }
            return true;
        }
        if (page == std::numeric_limits<int>::max()) { error = "Too many leaderboard API pages."; return false; }
        if (page >= pageLimit) { error = "Attempt import reached the 20,000-record limit; older evidence was not fetched."; return false; }
        previousBody = std::move(body);
        if (pace) std::this_thread::sleep_for(std::chrono::milliseconds(90));
    }
}

bool FetchAllPages(const std::function<std::string(int)>& urlForPage,
                   std::vector<MapEntry>& entries, std::string& error,
                   const HttpGetter& get = HttpGet, bool pace = true) {
    return FetchPages(urlForPage, entries, error, ParseEntries, get, pace);
}

bool ParseAttemptPage(const std::string& body, std::vector<Attempt>& attempts,
                      std::string& error, PageInfo* page, RatingSystem system) {
    // Reuse envelope validation and chart normalization without allowing
    // partial-run scores into the PP/accuracy profile.
    std::vector<MapEntry> unused;
    if (!ParseEntries(body, unused, error, page)) return false;
    rapidjson::Document doc;
    doc.Parse(body.c_str());
    const rapidjson::Value* rows = doc.IsArray() ? &doc : nullptr;
    if (doc.IsObject()) for (const auto* key : {"data", "playerScores"})
        if (doc.HasMember(key) && doc[key].IsArray()) { rows = &doc[key]; break; }
    if (!rows) { error = "Unexpected attempt page format."; return false; }
    for (const auto& row : rows->GetArray()) {
        if (!row.IsObject()) continue;
        if (system == RatingSystem::ScoreSaber && row.HasMember("leaderboard") && row["leaderboard"].IsObject()) {
            const auto& leaderboard = row["leaderboard"];
            if (leaderboard.HasMember("realm") && leaderboard["realm"].IsObject()) {
                const auto& realm = leaderboard["realm"];
                if (realm.HasMember("realmId") && realm["realmId"].IsNumber() && realm["realmId"].GetDouble() != 1.0) {
                    error = "ScoreSaber returned a different rating realm; attempt import skipped.";
                    return false;
                }
            }
        }
        const auto& result = row.HasMember("score") && row["score"].IsObject() ? row["score"] : row;
        Attempt a;
        a.source = system == RatingSystem::BeatLeader ? "beatleader" : "scoresaber";
        ReadString(result, "id", a.id);
        ReadTimestamp(result, "timepost", a.timestamp);
        if (a.timestamp <= 0) ReadTimestamp(result, "timeset", a.timestamp);
        if (a.timestamp <= 0) ReadTimestamp(result, "createdAt", a.timestamp);
        ReadNumber(result, system == RatingSystem::BeatLeader ? "time" : "playOutcomeTime", a.endTime);
        if (system == RatingSystem::BeatLeader) {
            if (result.HasMember("endType") && result["endType"].IsInt()) {
                const int type = result["endType"].GetInt();
                if (type == 1) a.outcome = AttemptOutcome::Clear;
                if (type == 2) a.outcome = AttemptOutcome::Fail;
                if (type == 3) a.outcome = AttemptOutcome::Restart;
                if (type == 4) a.outcome = AttemptOutcome::Quit;
                if (type == 5) a.outcome = AttemptOutcome::Practice;
            }
            double speed = 0, start = 0;
            ReadNumber(result, "speed", speed); ReadNumber(result, "startTime", start);
            a.practice = speed > 0.0001 || start > 0 || a.outcome == AttemptOutcome::Practice;
        } else {
            std::string outcome; ReadString(result, "playOutcome", outcome);
            if (outcome == "CLEAR") a.outcome = AttemptOutcome::Clear;
            if (outcome == "FAIL") a.outcome = AttemptOutcome::Fail;
            if (outcome == "QUIT") a.outcome = AttemptOutcome::Quit;
            if (outcome == "RESTART") a.outcome = AttemptOutcome::Restart;
        }
        Context modifiers; ReadNoFailModifier(result, modifiers);
        std::sort(modifiers.modifiers.begin(), modifiers.modifiers.end());
        for (const auto& token : modifiers.modifiers) { if (!a.modifiers.empty()) a.modifiers += ","; a.modifiers += token; }
        a.normal = !modifiers.noFail && a.modifiers.empty();
        std::vector<MapEntry> charts;
        VisitJson(row, Context{}, charts);
        if (charts.size() != 1 || charts.front().difficulties.size() != 1) continue;
        const auto& chart = charts.front();
        a.hash = chart.hash; a.characteristic = chart.difficulties.front().characteristic;
        a.difficulty = chart.difficulties.front().name;
        a.stars = chart.ranked ? chart.stars : 0.0;
        a.ratingChecked = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        if (!a.id.empty() && a.timestamp > 0) attempts.push_back(std::move(a));
    }
    return true;
}

std::string AttemptUtcDate(double timestamp) {
    const auto seconds = static_cast<std::time_t>(timestamp);
    std::tm date{};
    gmtime_r(&seconds, &date);
    std::ostringstream text; text << std::put_time(&date, "%Y-%m-%dT%H:%M:%SZ");
    return text.str();
}

void LoadAttempts(const std::string& id, RatingSystem system, ScoreHistory& history,
                  const HttpGetter& get, bool pace) {
    const std::string provider = system == RatingSystem::BeatLeader ? "beatleader" : "scoresaber";
    std::string warning;
    if (!ReadAttemptHistory(provider, id, history.attempts, warning)) history.attemptWarning = warning;
    const double now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    double since = now - 730.0 * 86400;
    for (const auto& a : history.attempts) if (a.source == provider) since = std::max(since, a.timestamp - 120.0);
    if (history.importAttempts) {
        std::vector<Attempt> fetched;
        warning.clear();
        HttpGetter compatibleGet = [get, system](const std::string& url, std::string& body, std::string& error) {
            if (get(url, body, error)) return true;
            // Some live v2 player routes reject realmId as a query string even
            // though it is documented. Retry using the default, then validate
            // every returned chart's realm in ParseAttemptPage.
            if (system == RatingSystem::ScoreSaber && error.find("HTTP 400") != std::string::npos) {
                auto fallback = url;
                const auto position = fallback.find("&realmId=1");
                if (position != std::string::npos) {
                    fallback.erase(position, std::string("&realmId=1").size());
                    error.clear();
                    return get(fallback, body, error);
                }
            }
            return false;
        };
        bool complete = FetchPages<Attempt>([&](int page) {
            std::ostringstream url;
            if (system == RatingSystem::BeatLeader)
                url << "https://api.beatleader.com/player/" << id << "/scoresstats?count=100&sortBy=date&order=desc&time_from="
                    << static_cast<long long>(since) << "&page=" << page;
            else url << "https://scoresaber.com/api/v2/players/" << id
                     << "/scores?personalBest=all&sort=recent&realmId=1&limit=100&from=" << AttemptUtcDate(since) << "&page=" << page;
            return url.str();
        }, fetched, warning, [system](const auto& body, auto& entries, auto& error, auto* info) {
            return ParseAttemptPage(body, entries, error, info, system);
        }, compatibleGet, pace, 200);
        if (!complete && warning.find("20,000-record limit") != std::string::npos) {
            complete = true; // Explicit bounded backfill, rather than a network gap.
            history.attemptWarning = warning;
        }
        // An interrupted import must not advance the saved cursor past unseen
        // pages. Keep the last complete cache and local records instead.
        if (complete) {
            std::vector<Attempt> merged;
            if (!SaveAttemptHistory(provider, id, fetched, merged, warning)) {
                MergeAttempts(history.attempts, fetched);
                history.attemptWarning = warning;
            } else history.attempts = std::move(merged);
        } else if (system == RatingSystem::BeatLeader && warning.find("HTTP 401") != std::string::npos) {
            history.attemptWarning = "BeatLeader attempt history requires public statistics (HTTP 401). "
                "Enable public statistics on your BeatLeader profile to import past attempts. "
                "Using cached/local attempts.";
        } else history.attemptWarning = (system == RatingSystem::BeatLeader ? "BeatLeader" : "ScoreSaber") +
                std::string(" attempt import unavailable: ") + warning + " Using cached/local evidence.";
    }
    ResolveAttemptRatings(history.attempts, history.scores);
    // Local charts may have no leaderboard score. Look their ratings up directly,
    // rather than assuming they fall inside the current recommendation band.
    std::unordered_set<std::string> lookedUp;
    int ratingErrors = 0;
    for (const auto& a : history.attempts) {
        const auto lookupKey = system == RatingSystem::BeatLeader ? Lower(a.hash) : AttemptChartKey(a);
        if (a.stars > 0 || !a.normal || a.practice || a.characteristic != "Standard" ||
            now - a.ratingChecked < 7.0 * 86400 ||
            (a.outcome != AttemptOutcome::Clear && a.outcome != AttemptOutcome::Fail) || lookedUp.contains(lookupKey)) continue;
        if (lookedUp.size() >= 100) { history.attemptWarning += " Rating lookup limited to 100 queries this refresh."; break; }
        lookedUp.insert(lookupKey);
        std::string body, error;
        const int difficulty = a.difficulty == "Easy" ? 1 : a.difficulty == "Normal" ? 3 : a.difficulty == "Hard" ? 5 :
            a.difficulty == "Expert" ? 7 : a.difficulty == "ExpertPlus" ? 9 : 0;
        if (system == RatingSystem::ScoreSaber && difficulty == 0) continue;
        const std::string url = system == RatingSystem::BeatLeader ? "https://api.beatleader.com/leaderboards/hash/" + a.hash :
            "https://scoresaber.com/api/v2/leaderboards/hash/" + a.hash + "/SoloStandard/" + std::to_string(difficulty) + "?realmId=1";
        if (get(url, body, error)) {
            std::vector<MapEntry> charts;
            if (ParseEntries(body, charts, error)) {
                ResolveAttemptRatings(history.attempts, charts);
                for (auto& run : history.attempts) {
                    const auto runLookup = system == RatingSystem::BeatLeader ? Lower(run.hash) : AttemptChartKey(run);
                    if (runLookup == lookupKey) run.ratingChecked = now;
                }
            } else ++ratingErrors;
        } else ++ratingErrors;
        if (pace) std::this_thread::sleep_for(std::chrono::milliseconds(90));
    }
    if (ratingErrors > 0) history.attemptWarning += " Ratings unavailable for " + std::to_string(ratingErrors) +
        " attempted maps; unrated attempts are excluded from the estimate.";
    if (!history.attempts.empty()) {
        std::vector<Attempt> merged;
        if (!SaveAttemptHistory(provider, id, history.attempts, merged, warning)) history.attemptWarning += " " + warning;
        else history.attempts = std::move(merged);
    }
}

bool IsDigits(const std::string& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(),
        [](unsigned char c) { return std::isdigit(c); });
}

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string EntryKey(const MapEntry& entry) {
    std::string key = Lower(entry.hash);
    for (const auto& difficulty : entry.difficulties) {
        key += "|" + Lower(difficulty.characteristic) + "|" + Lower(difficulty.name);
    }
    return key;
}

double EstimateTargetStars(const std::vector<MapEntry>& scores, RatingSystem system,
                           double minClearRate, const std::vector<Attempt>& attempts = {}) {
    const double target = EstimateRecommendationTargetStars(scores, system, minClearRate, attempts);
    return target > 0.0 ? target : kDefaultTargetStars;
}

bool LoadScoreHistory(const std::string& id, RatingSystem system, ScoreHistory& history,
                      std::string& error, const HttpGetter& get = HttpGet, bool pace = true, bool loadAttempts = true) {
    if (history.loaded && history.playerId == id && history.system == system) return true;
    const bool importAttempts = history.importAttempts;
    history = {};
    history.importAttempts = importAttempts;
    std::vector<MapEntry> scores;
    if (!FetchAllPages([&id, system](int page) {
        std::ostringstream url;
        if (system == RatingSystem::BeatLeader) {
            url << "https://api.beatleader.com/player/" << id
                << "/scores?sortBy=pp&order=desc&count=100&page=" << page;
        } else {
            url << "https://scoresaber.com/api/player/" << id
                << "/scores?limit=100&sort=top&page=" << page;
        }
        return url.str();
    }, scores, error, get, pace)) return false;
    history.playerId = id;
    history.system = system;
    history.scores = std::move(scores);
    history.loaded = true;
    if (loadAttempts) LoadAttempts(id, system, history, get, pace);
    return true;
}

StarRange MakeBeatLeaderNotPlayedStarRange(const std::vector<MapEntry>& scores,
                                          double minClearRate, const std::vector<Attempt>& attempts = {}) {
    const double targetStars = EstimateTargetStars(scores, RatingSystem::BeatLeader, minClearRate, attempts);
    return {std::max(1.0, targetStars - kTargetWindowStars),
            targetStars + kTargetWindowStars,
            true};
}

void FilterToTargetWindow(std::vector<MapEntry>& entries, double targetStars) {
    if (targetStars <= 0.0) return;
    for (auto& entry : entries) {
        entry.difficulties.erase(std::remove_if(entry.difficulties.begin(), entry.difficulties.end(),
            [targetStars](const Difficulty& difficulty) {
                return Lower(difficulty.characteristic) != "standard" || difficulty.stars <= 0.0 ||
                       std::abs(difficulty.stars - targetStars) > kTargetWindowStars;
            }), entry.difficulties.end());
    }
    entries.erase(std::remove_if(entries.begin(), entries.end(),
        [](const MapEntry& entry) { return entry.difficulties.empty(); }), entries.end());
}

bool FetchServiceRecommendations(RatingSystem system, const std::string& id,
                                  ListKind kind, int limit, std::vector<MapEntry>& result,
                                  std::string& error, double minClearRate,
                                  StarRange* notPlayedRange, ScoreHistory* cachedHistory,
                                  const HttpGetter& get = HttpGet, bool pace = true,
                                  bool rankByWeightedGain = false) {
    const bool beatLeader = system == RatingSystem::BeatLeader;
    if (!IsDigits(id)) {
        error = std::string("Enter a numeric ") + (beatLeader ? "BeatLeader" : "ScoreSaber") +
                " player ID in Mod Settings.";
        return false;
    }
    ScoreHistory localHistory;
    auto& history = cachedHistory ? *cachedHistory : localHistory;
    if (!LoadScoreHistory(id, system, history, error, get, pace)) return false;
    if (kind == ListKind::Combined) {
        std::vector<MapEntry> notPlayed, toImprove;
        // Rank and deduplicate each source by raw gain before merging. Trimming
        // chance-adjusted lists first can discard the best combined candidate.
        if (!FetchServiceRecommendations(system, id, ListKind::NotPlayed, -1, notPlayed, error,
              minClearRate, notPlayedRange, &history, get, pace, true) ||
            !FetchServiceRecommendations(system, id, ListKind::ToImprove, -1, toImprove, error,
              minClearRate, nullptr, &history, get, pace, true)) return false;
        result = MergeRecommendations(std::move(notPlayed), std::move(toImprove), limit);
        return true;
    }
    if (kind == ListKind::ToImprove) {
        result = SelectRecommendations(history.scores, kind, limit, 0.0, history.scores, system,
                                       minClearRate, rankByWeightedGain, history.attempts);
        return true;
    }

    const double targetStars = EstimateTargetStars(history.scores, system, minClearRate, history.attempts);
    const StarRange range{std::max(1.0, targetStars - kTargetWindowStars),
                          targetStars + kTargetWindowStars, true};
    if (beatLeader && notPlayedRange) *notPlayedRange = range;
    std::unordered_set<std::string> playedKeys;
    for (const auto& score : history.scores) {
        if (!score.noFail) playedKeys.insert(EntryKey(score));
    }

    std::vector<MapEntry> candidates;
    // Compare the entire star band. A raw row count cannot tell us whether
    // enough distinct, clearable songs exist or whether later pages are better.
    if (!FetchAllPages([beatLeader, range](int page) {
        std::ostringstream url;
        url.precision(12);
        if (beatLeader) {
            url << "https://api.beatleader.com/maps?type=ranked&mode=Standard&count=100"
                << "&stars_from=" << range.minStars << "&stars_to=" << range.maxStars
                << "&sortBy=stars&order=desc&page=" << page;
        } else {
            url << "https://scoresaber.com/api/v2/leaderboards?status=RANKED&realmId=1&limit=100"
                << "&minStars=" << range.minStars << "&maxStars=" << range.maxStars
                << "&sortBy=stars&sortDirection=desc&page=" << page;
        }
        return url.str();
    }, candidates, error, get, pace)) return false;

    FilterToTargetWindow(candidates, targetStars);
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
        [&playedKeys](const MapEntry& entry) { return playedKeys.contains(EntryKey(entry)); }), candidates.end());
    result = SelectRecommendations(std::move(candidates), kind, limit, targetStars,
                                   history.scores, system, minClearRate, rankByWeightedGain, history.attempts);
    return true;
}

bool FetchClanToConquerInternal(const std::string& playerId,
                               bool allClans,
                               double minStars,
                               double maxStars,
                               int limit,
                               std::vector<ClanPlaylistRecommendations>& clanPlaylists,
                                std::string& error,
                                const HttpGetter& get = HttpGet,
                                const HttpGetter& getIcon = HttpGetIcon) {
    clanPlaylists.clear();
    if (!IsDigits(playerId)) {
        error = "Enter a numeric BeatLeader player ID to load clan maps.";
        return false;
    }
    if (minStars > maxStars) std::swap(minStars, maxStars);

    std::string profileBody;
    if (!get("https://api.beatleader.com/player/" + playerId + "?stats=true", profileBody, error)) return false;
    rapidjson::Document profile;
    profile.Parse(profileBody.c_str());
    if (profile.HasParseError() || !profile.IsObject()) {
        error = "BeatLeader returned invalid profile data while loading clan membership.";
        return false;
    }

    if (!profile.HasMember("clans") || (!profile["clans"].IsArray() && !profile["clans"].IsNull())) {
        error = "BeatLeader profile did not include valid clan membership data.";
        return false;
    }
    std::vector<std::string> joinedTags;
    std::unordered_map<std::string, std::string> iconUrls;
    if (profile["clans"].IsArray()) {
        for (const auto& clan : profile["clans"].GetArray()) {
            std::string tag;
            if (!ReadString(clan, "tag", tag)) {
                error = "BeatLeader returned a clan without a tag.";
                return false;
            }
            if (std::find(joinedTags.begin(), joinedTags.end(), tag) == joinedTags.end()) joinedTags.push_back(tag);
            ReadString(clan, "icon", iconUrls[tag]);
        }
    }
    std::vector<std::string> orderedTags;
    std::string clanOrder;
    if (ReadString(profile, "clanOrder", clanOrder)) {
        std::istringstream tags(clanOrder);
        std::string tag;
        while (std::getline(tags, tag, ',')) {
            if (std::find(joinedTags.begin(), joinedTags.end(), tag) != joinedTags.end() &&
                std::find(orderedTags.begin(), orderedTags.end(), tag) == orderedTags.end()) {
                orderedTags.push_back(tag);
            }
        }
    }
    for (const auto& tag : joinedTags) {
        if (std::find(orderedTags.begin(), orderedTags.end(), tag) == orderedTags.end()) orderedTags.push_back(tag);
    }
    // Verified empty membership is a successful empty result, so refresh can
    // prune playlists from the previous membership. API failures still preserve them.
    if (orderedTags.empty()) return true;
    if (!allClans) orderedTags.resize(1);

    for (const auto& tag : orderedTags) {
        std::vector<MapEntry> clanCandidates;
        std::string iconUrl = iconUrls[tag];
        for (int page = 1; page <= 40; ++page) {
            std::ostringstream url;
            url << "https://api.beatleader.com/clan/" << tag
                << "/maps?page=" << page << "&count=100&sortBy=toconquer&order=desc";
            std::string body;
            if (!get(url.str(), body, error)) return false;

            rapidjson::Document document;
            document.Parse(body.c_str());
            if (document.HasParseError() || !document.IsObject() ||
                !document.HasMember("data") || !document["data"].IsArray()) {
                error = "BeatLeader returned invalid clan map data for " + tag + ".";
                return false;
            }
            const auto& rows = document["data"];
            // Clan map responses include the full clan under container; profile
            // membership usually contains only tag/id/color. No extra lookup needed.
            if (page == 1 && document.HasMember("container") && document["container"].IsObject()) {
                const auto& container = document["container"];
                std::string containerTag;
                if (ReadString(container, "tag", containerTag) && containerTag == tag)
                    ReadString(container, "icon", iconUrl);
            }
            for (const auto& row : rows.GetArray()) {
                if (!row.IsObject() || !row.HasMember("leaderboard") || !row["leaderboard"].IsObject()) continue;
                const auto& leaderboard = row["leaderboard"];
                if (!leaderboard.HasMember("song") || !leaderboard["song"].IsObject() ||
                    !leaderboard.HasMember("difficulty") || !leaderboard["difficulty"].IsObject()) continue;
                const auto& song = leaderboard["song"];
                const auto& chart = leaderboard["difficulty"];

                MapEntry entry;
                ReadString(song, "hash", entry.hash);
                ReadString(song, "name", entry.songName);
                ReadString(song, "subName", entry.songSubName);
                ReadString(song, "author", entry.songAuthorName);
                ReadString(song, "mapper", entry.levelAuthorName);
                ReadString(row, "leaderboardId", entry.leaderboardId);
                if (entry.leaderboardId.empty()) ReadString(leaderboard, "id", entry.leaderboardId);
                if (!row.HasMember("pp") || !row["pp"].IsNumber()) continue;
                entry.pp = row["pp"].GetDouble();

                std::string characteristic;
                std::string difficulty;
                ReadString(chart, "modeName", characteristic);
                ReadString(chart, "difficultyName", difficulty);
                if (difficulty.empty() || !chart.HasMember("stars") || !chart["stars"].IsNumber()) continue;
                entry.stars = chart["stars"].GetDouble();
                entry.difficulties.push_back({NormalizeCharacteristic(characteristic),
                                              NormalizeDifficulty(difficulty), entry.stars});
                clanCandidates.push_back(std::move(entry));
            }

            const auto pageBest = SelectClanRecommendations(clanCandidates, minStars, maxStars, limit);
            if (pageBest.size() >= static_cast<size_t>(std::max(limit, 1)) || rows.Empty()) break;
            if (rows.Size() < 100) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(90));
        }
        ClanPlaylistRecommendations playlist{tag, SelectClanRecommendations(
            std::move(clanCandidates), minStars, maxStars, limit)};
        std::string iconBody, iconError;
        if (Lower(iconUrl).starts_with("https://") && getIcon(iconUrl, iconBody, iconError) &&
            iconBody.size() <= kMaxClanIconBytes) playlist.image = DownloadedPlaylistImage(iconBody);
        if (playlist.image.empty()) {
            playlist.iconWarning = "Clan icon unavailable for " + tag + "; using the default cover.";
        }
        clanPlaylists.push_back(std::move(playlist));
    }
    return true;
}

} // namespace

bool FindAccounts(AccountService service, const std::string& input, int page,
                  AccountPage& result, std::string& error) {
    return FindAccountsInternal(service, input, page, result, error,
        [](const std::string& url, std::string& body, std::string& failure) {
            return HttpGetInternal(url, body, failure, 2 * 1024 * 1024, 12L);
        });
}

bool FetchAccountAvatar(const std::string& url, std::vector<std::uint8_t>& bytes) {
    bytes.clear();
    if (url.size() > 2048 || !Lower(url).starts_with("https://")) return false;
    std::string body, error;
    if (!HttpGetInternal(url, body, error, 1024 * 1024, 5L)) return false;
    // Only give Unity supported PNG/JPEG data; failed/unsupported avatars leave
    // the result selectable by name, country, rank and canonical ID.
    if (!IsAccountAvatarImage(body)) return false;
    bytes.assign(body.begin(), body.end());
    return true;
}

bool FetchBeatLeaderNotPlayedStarRange(const std::string& playerId,
                                       double minClearRate,
                                       StarRange& range,
                                       std::string& error,
                                       ScoreHistory* cachedHistory) {
    range = {};
    if (!IsDigits(playerId)) {
        error = "Enter a numeric BeatLeader player ID to calculate the automatic star range.";
        return false;
    }
    ScoreHistory localHistory;
    auto& history = cachedHistory ? *cachedHistory : localHistory;
    if (!LoadScoreHistory(playerId, RatingSystem::BeatLeader, history, error)) return false;
    range = MakeBeatLeaderNotPlayedStarRange(history.scores, minClearRate, history.attempts);
    return true;
}

bool FetchClanToConquer(const std::string& playerId,
                        bool allClans,
                        double minStars,
                        double maxStars,
                        int limit,
                        std::vector<ClanPlaylistRecommendations>& clanPlaylists,
                        std::string& error) {
    return FetchClanToConquerInternal(playerId, allClans, minStars, maxStars,
                                      limit, clanPlaylists, error);
}

bool FetchRecommendations(const std::string& service,
                          const std::string& playerId,
                          ListKind kind,
                          int limit,
                          std::vector<MapEntry>& entries,
                          std::string& error,
                          double minClearRate,
                          StarRange* beatLeaderNotPlayedRange,
                          ScoreHistory* history) {
    const auto system = Lower(service) == "scoresaber" ? RatingSystem::ScoreSaber : RatingSystem::BeatLeader;
    return FetchServiceRecommendations(system, playerId, kind, limit, entries, error, minClearRate,
                                       beatLeaderNotPlayedRange, history);
}

} // namespace rankedpractice
