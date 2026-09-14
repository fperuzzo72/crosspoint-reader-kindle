#pragma once

// mbedtls's SHA-256.
//
// Its caller here is FirmwareFlasher, verifying a downloaded image. That path
// does not apply on this target at all: the "firmware" is a file on a
// filesystem, not an image written to a flash slot, and OTA is not how this
// build updates.
//
// So rather than carry a SHA-256 implementation for a code path that cannot
// run, these report failure. A verification that cannot be performed must not
// return success.

#include <cstddef>
#include <cstdint>

struct mbedtls_sha256_context {
  int unused;
};

inline void mbedtls_sha256_init(mbedtls_sha256_context*) {}
inline void mbedtls_sha256_free(mbedtls_sha256_context*) {}
inline int mbedtls_sha256_starts(mbedtls_sha256_context*, int) { return -1; }
inline int mbedtls_sha256_update(mbedtls_sha256_context*, const unsigned char*, size_t) { return -1; }
inline int mbedtls_sha256_finish(mbedtls_sha256_context*, unsigned char*) { return -1; }
inline int mbedtls_sha256(const unsigned char*, size_t, unsigned char*, int) { return -1; }
