#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rankedpractice {

enum class AccountService { BeatLeader, ScoreSaber };
enum class AccountInputKind { Search, Profile, NumericId };

struct AccountQuery {
    AccountInputKind kind{AccountInputKind::Search};
    std::string value;
};

struct AccountProfile {
    std::string id, name, avatar, country;
    std::int64_t rank{0};
};

struct AccountPage {
    std::vector<AccountProfile> profiles;
    int page{1};
    bool hasNext{false};
};

// These helpers never save a selection. Every result contains a canonical ID.
bool ParseAccountInput(AccountService service, const std::string& input,
                       AccountQuery& query, std::string& error);
std::string AccountRequestUrl(AccountService service, const AccountQuery& query, int page);
bool ParseAccountPage(AccountService service, const AccountQuery& query, int page,
                      const std::string& body, AccountPage& result, std::string& error);
bool IsCanonicalAccountId(const std::string& id);
bool IsAccountAvatarImage(std::string_view bytes);

bool FindAccounts(AccountService service, const std::string& input, int page,
                  AccountPage& result, std::string& error);
bool FetchAccountAvatar(const std::string& url, std::vector<std::uint8_t>& bytes);

// Main-thread selection state: invalidating an input/request makes old replies
// and buttons unusable, even if a detached HTTP request completes afterward.
struct AccountLookupState {
    std::uint64_t revision{0};
    bool loading{false};
    AccountPage results;
    std::uint64_t Begin();
    void Invalidate();
    bool Complete(std::uint64_t request, AccountPage page);
    const AccountProfile* Select(std::uint64_t request, std::size_t index) const;
};

} // namespace rankedpractice
