#ifndef RANKEDTOOLS_THREADING_ALT_H
#define RANKEDTOOLS_THREADING_ALT_H

/* C-compatible layout; the C++ adapter owns the std::mutex behind this pointer. */
typedef struct mbedtls_threading_mutex_t {
    void *implementation;
} mbedtls_threading_mutex_t;

#endif
