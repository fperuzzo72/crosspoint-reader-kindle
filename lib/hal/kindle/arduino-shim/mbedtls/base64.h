#pragma once

// mbedtls's base64, mapped onto the implementation this port already has and
// already tested against RFC 4648. Not a stub: the callers obfuscate stored
// credentials with it, and a wrong answer would corrupt them silently.

#include <cstddef>
#include <cstdint>

int mbedtls_base64_encode(unsigned char* dst, size_t dlen, size_t* olen, const unsigned char* src, size_t slen);
int mbedtls_base64_decode(unsigned char* dst, size_t dlen, size_t* olen, const unsigned char* src, size_t slen);

#define MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL -0x002A
#define MBEDTLS_ERR_BASE64_INVALID_CHARACTER -0x002C
