// Exercise the actual Mbed TLS mutex callbacks and shared PSA key slots.
#include "https_threading.hpp"
#include <psa/crypto.h>
#include <array>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error(#condition); } while (false)
}

int main() {
    using namespace rankedpractice::https;
    try {
        constexpr int workerCount = 8;
        constexpr int iterations = 300;
        std::atomic<int> ready{0}, failures{0};
        std::vector<std::thread> workers;
        // Race the first initialization deliberately: callback installation must
        // finish exactly once before any worker enters the PSA implementation.
        for (int i = 0; i < workerCount; ++i) workers.emplace_back([&] {
            ++ready;
            while (ready.load() != workerCount) std::this_thread::yield();
            InitializeThreading();
            if (psa_crypto_init() != PSA_SUCCESS) { ++failures; return; }
            for (int n = 0; n < iterations; ++n) {
                std::array<unsigned char, 16> keyBytes{};
                if (psa_generate_random(keyBytes.data(), keyBytes.size()) != PSA_SUCCESS) ++failures;
                psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
                psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
                psa_set_key_bits(&attributes, 128);
                psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_EXPORT);
                mbedtls_svc_key_id_t key = 0;
                const auto status = psa_import_key(&attributes, keyBytes.data(), keyBytes.size(), &key);
                psa_reset_key_attributes(&attributes);
                if (status != PSA_SUCCESS) { ++failures; continue; }
                std::array<unsigned char, 16> exported{};
                size_t length = 0;
                if (psa_export_key(key, exported.data(), exported.size(), &length) != PSA_SUCCESS ||
                    length != keyBytes.size() || exported != keyBytes) ++failures;
                if (psa_destroy_key(key) != PSA_SUCCESS) ++failures;
            }
        });
        for (auto& worker : workers) worker.join();
        CHECK(failures == 0);
        CHECK(mbedtls_mutex_lock == MutexLock && mbedtls_mutex_unlock == MutexUnlock);
        CHECK(MutexLock(nullptr) == MBEDTLS_ERR_THREADING_MUTEX_ERROR);
        CHECK(MutexUnlock(nullptr) == MBEDTLS_ERR_THREADING_MUTEX_ERROR);
        MutexInit(nullptr);
        MutexFree(nullptr);
        mbedtls_threading_mutex_t mutex{};
        CHECK(MutexLock(&mutex) == MBEDTLS_ERR_THREADING_MUTEX_ERROR);
        MutexInit(&mutex);
        CHECK(mutex.implementation != nullptr);
        int protectedCount = 0;
        workers.clear();
        for (int i = 0; i < workerCount; ++i) workers.emplace_back([&] {
            for (int n = 0; n < 10000; ++n) {
                if (mbedtls_mutex_lock(&mutex) != 0) { ++failures; return; }
                ++protectedCount;
                if (mbedtls_mutex_unlock(&mutex) != 0) { ++failures; return; }
            }
        });
        for (auto& worker : workers) worker.join();
        CHECK(failures == 0 && protectedCount == workerCount * 10000);
        MutexFree(&mutex);
        CHECK(mutex.implementation == nullptr);
        CHECK(MutexLock(&mutex) == MBEDTLS_ERR_THREADING_MUTEX_ERROR);
        CHECK(MutexUnlock(&mutex) == MBEDTLS_ERR_THREADING_MUTEX_ERROR);
        std::cout << "All " << checks << " HTTPS threading checks passed; " << workerCount * iterations
                  << " concurrent PSA key lifecycles and " << protectedCount << " protected increments.\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
