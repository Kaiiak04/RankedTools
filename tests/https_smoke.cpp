// Opt-in real-network validation of the exact source-built curl/Mbed TLS stack.
// Run from the repository root so the bundled CA file is used.
#define RANKEDTOOLS_CA_BUNDLE_PATH "cacert.pem"
#include "libcurl/shared/curl.h"
// Android's resolver uses Android services unavailable in Docker Desktop WSL.
// Optional host:443:ip arguments supply DNS results without changing URLs, SNI,
// certificate checks, or the production client's other options.
static curl_slist* smokeHosts = nullptr;
template<class T> CURLcode SmokeSetopt(CURL* curl, CURLoption option, T value) {
    const auto result = curl_easy_setopt(curl, option, value);
    if (option == CURLOPT_URL && smokeHosts) curl_easy_setopt(curl, CURLOPT_RESOLVE, smokeHosts);
    return result;
}
#undef curl_easy_setopt
#define curl_easy_setopt SmokeSetopt
#include "../src/recommendation_api.cpp"
#include <array>
#include <iostream>

int main(int argc, char** argv) {
    using namespace rankedpractice;
    const auto* version = curl_version_info(CURLVERSION_NOW);
    std::cout << version->version << " / " << version->ssl_version << '\n';
    if (std::string(version->ssl_version).find("mbedTLS/3.6.7") == std::string::npos) return 1;
    for (int i = 1; i < argc; ++i) smokeHosts = curl_slist_append(smokeHosts, argv[i]);
    std::string body, error;
    for (const auto* url : {
             "https://api.beatleader.com/maps?type=ranked&mode=Standard&count=1",
             "https://api.beatleader.com/maps?type=ranked&mode=Standard&count=1",
             "https://scoresaber.com/api/leaderboards?ranked=true&limit=1",
             "https://api.accsaber.com/v1/categories"}) {
        if (!HttpGetInternal(url, body, error, 1024*1024, 30) || body.empty()) {
            std::cerr << url << ": " << error << '\n'; return 1;
        }
        std::cout << "PASS HTTPS: " << url << " (" << body.size() << " bytes)\n";
    }
    const std::array<const char*, 3> hosts{
        "https://api.beatleader.com/maps?type=ranked&mode=Standard&count=1",
        "https://scoresaber.com/api/leaderboards?ranked=true&limit=1",
        "https://api.accsaber.com/v1/categories"};
    std::array<std::string, 3> failures;
    std::vector<std::thread> workers;
    for (size_t i = 0; i < hosts.size(); ++i) workers.emplace_back([&, i] {
        for (int request = 0; request < 3; ++request) {
            std::string bytes, failure;
            if (!HttpGetInternal(hosts[i], bytes, failure, 1024*1024, 30) || bytes.empty()) {
                failures[i] = failure.empty() ? "Empty API response" : failure;
                return;
            }
        }
    });
    for (auto& worker : workers) worker.join();
    for (size_t i = 0; i < hosts.size(); ++i) {
        if (!failures[i].empty()) {
            std::cerr << hosts[i] << ": " << failures[i] << '\n';
            return 1;
        }
        std::cout << "PASS concurrent HTTPS: " << hosts[i] << " (3 requests)\n";
    }
    if (HttpGetInternal("http://api.beatleader.com", body, error, 1024, 5)) return 1;
    std::cout << "PASS: HTTP rejected\n";
    // A fresh worker prevents an existing verified connection from masking the
    // invalid CA, and exercises production error reporting rather than raw curl.
    const auto validBundle = RuntimePaths().caBundle;
    RuntimePaths().caBundle = "tests/no-such-ca-file.pem";
    bool acceptedInvalidCa = false;
    std::thread invalidCa([&] {
        acceptedInvalidCa = HttpGetInternal("https://api.beatleader.com", body, error, 1024, 15);
    });
    invalidCa.join();
    RuntimePaths().caBundle = validBundle;
    if (acceptedInvalidCa ||
        (error.find("(curl 77): ") == std::string::npos && error.find("(curl 60): ") == std::string::npos))
        return 1;
    std::cout << "PASS: invalid CA rejected with detail: " << error << '\n';
    curl_slist_free_all(smokeHosts);
}
