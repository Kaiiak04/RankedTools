// Exercise the production HTTPS client with a fake libcurl boundary. No network.
#include <atomic>
#include <cstdarg>
#include <iostream>
#include <stdexcept>
#include <thread>
#include "libcurl/shared/curl.h"

#undef curl_easy_setopt
#undef curl_easy_getinfo
CURLcode MockEasySetopt(CURL* curl, int option, ...);
CURLcode MockEasyGetinfo(CURL* curl, int info, ...);
#define curl_easy_setopt MockEasySetopt
#define curl_easy_getinfo MockEasyGetinfo
#include "../src/recommendation_api.cpp"
#undef curl_easy_setopt
#undef curl_easy_getinfo

namespace {
struct Options {
    std::string url, protocols, redirectProtocols;
    long timeout{0}, connectTimeout{0}, noSignal{0};
    curl_write_callback write{nullptr};
    void* data{nullptr};
};
struct FakeHandle {
    Options options;
    std::thread::id owner{std::this_thread::get_id()};
    bool connected{false};
    long newConnections{0};
};
std::atomic<int> created{0}, destroyed{0};
std::atomic<bool> invalidOptions{false};
thread_local FakeHandle* current{nullptr};
thread_local std::string responseBody{"ok"};
thread_local long responseCode{200};
thread_local CURLcode responseResult{CURLE_OK};
thread_local long expectedTimeout{18};
int checks{0};
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error(#condition); } while (false)
}

extern "C" CURLcode curl_global_init(long) { return CURLE_OK; }
extern "C" CURL* curl_easy_init() {
    ++created;
    current = new FakeHandle;
    return reinterpret_cast<CURL*>(current);
}
extern "C" void curl_easy_cleanup(CURL* curl) {
    ++destroyed;
    delete reinterpret_cast<FakeHandle*>(curl);
    current = nullptr;
}
extern "C" void curl_easy_reset(CURL* curl) {
    auto& handle = *reinterpret_cast<FakeHandle*>(curl);
    if (handle.owner != std::this_thread::get_id()) invalidOptions = true;
    handle.options = {};
}
CURLcode MockEasySetopt(CURL* curl, int option, ...) {
    auto& options = reinterpret_cast<FakeHandle*>(curl)->options;
    va_list args; va_start(args, option);
    switch (option) {
        case CURLOPT_URL: options.url = va_arg(args, const char*); break;
        case CURLOPT_PROTOCOLS_STR: options.protocols = va_arg(args, const char*); break;
        case CURLOPT_REDIR_PROTOCOLS_STR: options.redirectProtocols = va_arg(args, const char*); break;
        case CURLOPT_USERAGENT: case CURLOPT_CAINFO: case CURLOPT_CAPATH: (void)va_arg(args, const char*); break;
        case CURLOPT_TIMEOUT: options.timeout = va_arg(args, long); break;
        case CURLOPT_CONNECTTIMEOUT: options.connectTimeout = va_arg(args, long); break;
        case CURLOPT_NOSIGNAL: options.noSignal = va_arg(args, long); break;
        case CURLOPT_FOLLOWLOCATION: case CURLOPT_MAXREDIRS: (void)va_arg(args, long); break;
        case CURLOPT_WRITEFUNCTION: options.write = va_arg(args, curl_write_callback); break;
        case CURLOPT_WRITEDATA: options.data = va_arg(args, void*); break;
        default: invalidOptions = true;
    }
    va_end(args);
    return CURLE_OK;
}
extern "C" CURLcode curl_easy_perform(CURL* curl) {
    auto& handle = *reinterpret_cast<FakeHandle*>(curl);
    const auto& options = handle.options;
    if (handle.owner != std::this_thread::get_id() || options.protocols != "https" ||
        options.redirectProtocols != "https" || options.noSignal != 1 ||
        !options.write || !options.data || options.timeout != expectedTimeout ||
        options.connectTimeout != std::min(10L, expectedTimeout))
        invalidOptions = true;
    handle.newConnections = handle.connected ? 0 : 1;
    handle.connected = true;
    if (responseResult != CURLE_OK) return responseResult;
    return options.write(responseBody.data(), 1, responseBody.size(), options.data) == responseBody.size()
        ? CURLE_OK : CURLE_WRITE_ERROR;
}
CURLcode MockEasyGetinfo(CURL* curl, int info, ...) {
    va_list args; va_start(args, info);
    if (info == CURLINFO_RESPONSE_CODE) *va_arg(args, long*) = responseCode;
    else if (info == CURLINFO_NUM_CONNECTS) *va_arg(args, long*) = reinterpret_cast<FakeHandle*>(curl)->newConnections;
    else if (info == CURLINFO_TOTAL_TIME) *va_arg(args, double*) = .25;
    else invalidOptions = true;
    va_end(args);
    return CURLE_OK;
}
extern "C" const char* curl_easy_strerror(CURLcode) { return "mock failure"; }

int main() {
    using namespace rankedpractice;
    try {
        std::string body, error;
        CHECK(HttpGetInternal("https://example.test/first", body, error, 100, 18));
        CHECK(body == "ok" && created == 1);
        CHECK(current->options.data == nullptr && current->options.write == nullptr);
        responseBody = "12345";
        expectedTimeout = 8;
        CHECK(!HttpGetInternal("https://example.test/icon", body, error, 4, 8));
        CHECK(!error.empty() && body.empty());
        CHECK(current->options.data == nullptr);
        expectedTimeout = 18;
        CHECK(HttpGetInternal("https://example.test/next", body, error, 100, 18));
        CHECK(body == "12345" && created == 1);
        responseCode = 401;
        CHECK(!HttpGetInternal("https://example.test/private", body, error, 100, 18));
        CHECK(error.find("HTTP 401") != std::string::npos);
        responseCode = 200;
        responseResult = CURLE_OPERATION_TIMEDOUT;
        CHECK(!HttpGetInternal("https://example.test/timeout", body, error, 100, 18));
        responseResult = CURLE_OK;
        CHECK(HttpGetInternal("https://example.test/recovered", body, error, 100, 18));
        const auto statistics = GetHttpStatistics();
        CHECK(statistics.requests == 6 && statistics.connections == 1 && statistics.seconds == 1.5);
        bool workerSucceeded = false;
        std::thread worker([&] {
            std::string bytes, message;
            expectedTimeout = 5;
            workerSucceeded = HttpGetInternal("https://example.test/other", bytes, message, 100, 5) &&
                GetHttpStatistics().requests == 1 && GetHttpStatistics().connections == 1;
        });
        worker.join();
        CHECK(workerSucceeded && created == 2 && destroyed == 1);
        CHECK(GetHttpStatistics().requests == 6);
        CHECK(!invalidOptions);
        std::cout << "All " << checks << " HTTPS client checks passed.\n";
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n'; return 1;
    }
}
