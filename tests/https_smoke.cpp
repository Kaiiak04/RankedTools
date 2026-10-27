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
             "https://scoresaber.com/api/leaderboards?ranked=true&limit=1"}) {
        if (!HttpGetInternal(url, body, error, 1024*1024, 30) || body.empty()) {
            std::cerr << url << ": " << error << '\n'; return 1;
        }
        std::cout << "PASS HTTPS: " << url << " (" << body.size() << " bytes)\n";
    }
    if (HttpGetInternal("http://api.beatleader.com", body, error, 1024, 5)) return 1;
    std::cout << "PASS: HTTP rejected\n";
    CURL* invalidCa = curl_easy_init();
    curl_easy_setopt(invalidCa, CURLOPT_URL, "https://api.beatleader.com");
    curl_easy_setopt(invalidCa, CURLOPT_CAINFO, "tests/no-such-ca-file.pem");
    curl_easy_setopt(invalidCa, CURLOPT_TIMEOUT, 15L);
    const auto result = curl_easy_perform(invalidCa);
    curl_easy_cleanup(invalidCa);
    if (result != CURLE_SSL_CACERT_BADFILE && result != CURLE_PEER_FAILED_VERIFICATION) return 1;
    std::cout << "PASS: invalid CA rejected\n";
    curl_slist_free_all(smokeHosts);
}
