#include "account.hpp"

#include <rapidjson/document.h>
#include <algorithm>
#include <cctype>
#include <limits>
#include <unordered_set>

namespace rankedpractice {
namespace {

std::string AccountLower(std::string value) {
    for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool AccountControls(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
}

std::string EncodeAccountComponent(const std::string& value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') encoded += c;
        else { encoded += '%'; encoded += hex[c >> 4]; encoded += hex[c & 15]; }
    }
    return encoded;
}

bool DecodeAccountComponent(const std::string& value, std::string& decoded) {
    auto digit = [](unsigned char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    decoded.clear();
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '%') decoded += value[i];
        else {
            if (i + 2 >= value.size()) return false;
            int a = digit(value[i + 1]), b = digit(value[i + 2]);
            if (a < 0 || b < 0) return false;
            decoded += static_cast<char>((a << 4) | b);
            i += 2;
        }
    }
    return !decoded.empty() && !AccountControls(decoded) && decoded.find_first_of("/\\?#") == std::string::npos;
}

std::string AccountString(const rapidjson::Value& object, const char* field) {
    if (!object.IsObject() || !object.HasMember(field)) return {};
    const auto& value = object[field];
    if (value.IsString()) return {value.GetString(), value.GetStringLength()};
    if (value.IsUint64()) return std::to_string(value.GetUint64());
    return {};
}

std::int64_t AccountInteger(const rapidjson::Value& object, const char* field) {
    if (!object.IsObject() || !object.HasMember(field) || !object[field].IsInt64()) return 0;
    return std::max<std::int64_t>(0, object[field].GetInt64());
}

bool ReadAccountProfile(AccountService service, const rapidjson::Value& value, AccountProfile& profile) {
    profile.id = AccountString(value, "id");
    profile.name = AccountString(value, "name");
    if (!IsCanonicalAccountId(profile.id) || profile.name.empty() || profile.name.size() > 256 ||
        AccountControls(profile.name)) return false;
    profile.country = AccountString(value, "country");
    if (profile.country.size() != 2 || !std::all_of(profile.country.begin(), profile.country.end(),
        [](unsigned char c) { return std::isalpha(c); })) profile.country.clear();
    profile.avatar = AccountString(value, "avatar");
    if (profile.avatar.size() > 2048 || !AccountLower(profile.avatar).starts_with("https://") ||
        AccountControls(profile.avatar)) profile.avatar.clear();
    if (service == AccountService::ScoreSaber && value.HasMember("stats"))
        profile.rank = AccountInteger(value["stats"], "rank");
    else profile.rank = AccountInteger(value, "rank");
    return true;
}

} // namespace

bool IsCanonicalAccountId(const std::string& id) {
    return !id.empty() && id.size() <= 32 &&
        std::all_of(id.begin(), id.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }) &&
        id.find_first_not_of('0') != std::string::npos;
}

bool IsAccountAvatarImage(std::string_view bytes) {
    auto dimensionAllowed = [](std::uint32_t width, std::uint32_t height) {
        return width > 0 && height > 0 && width <= 1024 && height <= 1024;
    };
    if (bytes.size() >= 45 && bytes.substr(0, 8) == std::string_view("\x89PNG\r\n\x1a\n", 8) &&
        bytes.substr(12, 4) == "IHDR" && bytes.substr(bytes.size() - 8, 4) == "IEND") {
        auto number = [&](std::size_t offset) {
            std::uint32_t value = 0;
            for (int i = 0; i < 4; ++i) value = (value << 8) | static_cast<unsigned char>(bytes[offset + i]);
            return value;
        };
        return dimensionAllowed(number(16), number(20));
    }
    if (bytes.size() < 20 || bytes.substr(0, 2) != std::string_view("\xff\xd8", 2) ||
        bytes.substr(bytes.size() - 2) != std::string_view("\xff\xd9", 2)) return false;
    // Find a JPEG frame header before letting Unity allocate its decoded texture.
    std::size_t offset = 2;
    while (offset < bytes.size()) {
        if (static_cast<unsigned char>(bytes[offset++]) != 0xff) return false;
        while (offset < bytes.size() && static_cast<unsigned char>(bytes[offset]) == 0xff) ++offset;
        if (offset >= bytes.size()) return false;
        const auto marker = static_cast<unsigned char>(bytes[offset++]);
        if (marker == 0xda || marker == 0xd9) return false;
        if (marker == 1 || (marker >= 0xd0 && marker <= 0xd7)) continue;
        if (offset + 2 > bytes.size()) return false;
        auto number = [&](std::size_t pos) {
            return (static_cast<unsigned char>(bytes[pos]) << 8) | static_cast<unsigned char>(bytes[pos + 1]);
        };
        const auto length = number(offset);
        if (length < 2 || static_cast<std::size_t>(length) > bytes.size() - offset) return false;
        if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc)
            return length >= 8 && dimensionAllowed(number(offset + 5), number(offset + 3));
        offset += length;
    }
    return false;
}

bool ParseAccountInput(AccountService service, const std::string& input,
                       AccountQuery& query, std::string& error) {
    query = {}; error.clear();
    auto first = input.find_first_not_of(" \t\r\n"), last = input.find_last_not_of(" \t\r\n");
    std::string value = first == std::string::npos ? "" : input.substr(first, last - first + 1);
    if (value.empty() || value.size() > 1024 || AccountControls(value)) {
        error = "Enter an account name, profile link, or numeric ID."; return false;
    }
    auto lower = AccountLower(value);
    bool url = value.find("://") != std::string::npos || lower.starts_with("www.") ||
        lower.starts_with("beatleader.com/") || lower.starts_with("beatleader.xyz/") ||
        lower.starts_with("scoresaber.com/");
    if (url) {
        auto scheme = lower.find("://");
        if (scheme != std::string::npos) {
            if (lower.substr(0, scheme) != "https" && lower.substr(0, scheme) != "http") {
                error = "Use a BeatLeader or ScoreSaber profile link."; return false;
            }
            value.erase(0, scheme + 3);
        }
        auto slash = value.find('/');
        auto host = AccountLower(value.substr(0, slash));
        if (host.starts_with("www.")) host.erase(0, 4);
        bool bl = host == "beatleader.com" || host == "beatleader.xyz";
        bool ss = host == "scoresaber.com";
        if (!(service == AccountService::BeatLeader ? bl : ss) || slash == std::string::npos) {
            error = "Use a profile link for this leaderboard."; return false;
        }
        auto path = value.substr(slash);
        if (!path.starts_with("/u/")) { error = "The profile link must contain /u/ followed by the account."; return false; }
        auto end = path.find_first_of("/?#", 3);
        if (!DecodeAccountComponent(path.substr(3, end == std::string::npos ? end : end - 3), value) || value.size() > 128) {
            error = "The profile link has an invalid account name or ID."; return false;
        }
        query = {IsCanonicalAccountId(value) ? AccountInputKind::NumericId : AccountInputKind::Profile, value};
        return true;
    }
    if (std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) {
        if (!IsCanonicalAccountId(value)) { error = "That numeric account ID is invalid."; return false; }
        query = {AccountInputKind::NumericId, value}; return true;
    }
    const auto characters = std::count_if(value.begin(), value.end(), [](unsigned char c) { return (c & 0xc0) != 0x80; });
    if (value.size() > 128) { error = "Enter a shorter account name (up to 128 bytes)."; return false; }
    if (service == AccountService::ScoreSaber && characters < 3) {
        error = "ScoreSaber searches need at least 3 characters. You can also enter a profile link or ID."; return false;
    }
    query = {AccountInputKind::Search, value}; return true;
}

std::string AccountRequestUrl(AccountService service, const AccountQuery& query, int page) {
    auto component = EncodeAccountComponent(query.value);
    if (service == AccountService::BeatLeader) {
        if (query.kind != AccountInputKind::Search) return "https://api.beatleader.com/player/" + component;
        return "https://api.beatleader.com/players?search=" + component + "&count=5&page=" + std::to_string(std::max(1, page));
    }
    if (query.kind == AccountInputKind::NumericId) return "https://scoresaber.com/api/v2/players/" + component;
    if (query.kind == AccountInputKind::Profile) return "https://scoresaber.com/api/v2/players/vanity/" + component;
    return "https://scoresaber.com/api/v2/players?search=" + component + "&limit=5&page=" + std::to_string(std::max(1, page));
}

bool ParseAccountPage(AccountService service, const AccountQuery& query, int page,
                      const std::string& body, AccountPage& result, std::string& error) {
    result = {}; result.page = std::max(1, page); error.clear();
    rapidjson::Document json;
    json.Parse(body.data(), body.size());
    if (json.HasParseError() || !json.IsObject()) { error = "The account API returned an invalid response."; return false; }
    if (query.kind != AccountInputKind::Search) {
        AccountProfile profile;
        if (!ReadAccountProfile(service, json, profile)) {
            error = "The account could not be verified."; return false;
        }
        if (query.kind == AccountInputKind::NumericId && profile.id != query.value) {
            // BeatLeader also accepts a linked Quest/Steam/Oculus ID and returns
            // the primary profile. Verify that relationship before saving it.
            bool linked = false;
            if (service == AccountService::BeatLeader && json.HasMember("linkedIds") && json["linkedIds"].IsObject()) {
                for (const char* key : {"questId", "steamId", "oculusPCId"})
                    linked = linked || AccountString(json["linkedIds"], key) == query.value;
            }
            if (!linked) { error = "The account could not be verified."; return false; }
        }
        result.profiles.push_back(std::move(profile)); return true;
    }
    if (!json.HasMember("data") || !json["data"].IsArray() || json["data"].Size() > 5) {
        error = "The account search returned an invalid response."; return false;
    }
    std::unordered_set<std::string> seen;
    for (const auto& row : json["data"].GetArray()) {
        AccountProfile profile;
        if (ReadAccountProfile(service, row, profile) && seen.insert(profile.id).second)
            result.profiles.push_back(std::move(profile));
    }
    if (json.HasMember("metadata") && json["metadata"].IsObject()) {
        const auto& meta = json["metadata"];
        auto returnedPage = AccountInteger(meta, "page");
        auto perPage = AccountInteger(meta, "itemsPerPage");
        if (returnedPage && returnedPage != result.page) { error = "The account API returned a different page. Try searching again."; result.profiles.clear(); return false; }
        if (!perPage) perPage = 5;
        auto total = AccountInteger(meta, service == AccountService::BeatLeader ? "total" : "totalItems");
        result.hasNext = total / perPage > result.page || (total / perPage == result.page && total % perPage != 0);
    } else result.hasNext = json["data"].Size() == 5;
    return true;
}

std::uint64_t AccountLookupState::Begin() { Invalidate(); loading = true; return revision; }
void AccountLookupState::Invalidate() { ++revision; loading = false; results = {}; }
bool AccountLookupState::Complete(std::uint64_t request, AccountPage page) {
    if (request != revision || !loading) return false;
    results = std::move(page); loading = false; return true;
}
const AccountProfile* AccountLookupState::Select(std::uint64_t request, std::size_t index) const {
    if (request != revision || loading || index >= results.profiles.size()) return nullptr;
    return &results.profiles[index];
}

} // namespace rankedpractice
