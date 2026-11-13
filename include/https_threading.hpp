#pragma once

#include <mbedtls/threading.h>
#include <mutex>
#include <new>

#if !defined(MBEDTLS_THREADING_C) || !defined(MBEDTLS_THREADING_ALT)
#error "The HTTPS stack requires the RankedTools Mbed TLS threading configuration."
#endif

namespace rankedpractice::https {

inline void MutexInit(mbedtls_threading_mutex_t* mutex) noexcept {
    if (!mutex) return;
    mutex->implementation = nullptr;
    try {
        mutex->implementation = new (std::nothrow) std::mutex;
    } catch (...) {
        // An unsuccessful init must remain safe to lock and free through C.
    }
}

inline void MutexFree(mbedtls_threading_mutex_t* mutex) noexcept {
    if (!mutex) return;
    delete static_cast<std::mutex*>(mutex->implementation);
    mutex->implementation = nullptr;
}

inline int MutexLock(mbedtls_threading_mutex_t* mutex) noexcept {
    if (!mutex || !mutex->implementation) return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
    try {
        static_cast<std::mutex*>(mutex->implementation)->lock();
        return 0;
    } catch (...) {
        return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
    }
}

inline int MutexUnlock(mbedtls_threading_mutex_t* mutex) noexcept {
    if (!mutex || !mutex->implementation) return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
    try {
        static_cast<std::mutex*>(mutex->implementation)->unlock();
        return 0;
    } catch (...) {
        return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
    }
}

inline void InitializeThreading() {
    // The static guard finishes callback/global-mutex setup before any worker
    // enters curl/PSA. Keep them alive for thread-local curl handle destruction;
    // freeing them at refresh completion or during static teardown is too early.
    static const bool ready = [] {
        mbedtls_threading_set_alt(MutexInit, MutexFree, MutexLock, MutexUnlock);
        return true;
    }();
    (void)ready;
}

} // namespace rankedpractice::https
