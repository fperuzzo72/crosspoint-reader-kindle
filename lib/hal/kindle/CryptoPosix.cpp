// base64 and MD5 for the Kindle build.
//
// Both are implemented rather than stubbed, and for the same reason: their
// callers use the output as an identity, not as decoration. A fake base64
// authenticates as nobody; a fake MD5 makes every book the same document to
// KOReader's sync and cross-contaminates reading positions between them.

#include <cstdio>
#include <cstring>

#include "arduino-shim/MD5Builder.h"
#include "arduino-shim/base64.h"

namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int decodeChar(const char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

}  // namespace

// ---------------------------------------------------------------- base64 ---

String base64::encode(const uint8_t* data, const size_t length) {
  if (data == nullptr || length == 0) {
    return String();
  }
  std::string out;
  out.reserve(((length + 2) / 3) * 4);

  size_t i = 0;
  while (i + 2 < length) {
    const uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) |
                            static_cast<uint32_t>(data[i + 2]);
    out += kAlphabet[(triple >> 18) & 0x3F];
    out += kAlphabet[(triple >> 12) & 0x3F];
    out += kAlphabet[(triple >> 6) & 0x3F];
    out += kAlphabet[triple & 0x3F];
    i += 3;
  }

  // Tail: one or two bytes, padded with '='.
  if (i < length) {
    const size_t remaining = length - i;
    uint32_t triple = static_cast<uint32_t>(data[i]) << 16;
    if (remaining == 2) {
      triple |= static_cast<uint32_t>(data[i + 1]) << 8;
    }
    out += kAlphabet[(triple >> 18) & 0x3F];
    out += kAlphabet[(triple >> 12) & 0x3F];
    out += remaining == 2 ? kAlphabet[(triple >> 6) & 0x3F] : '=';
    out += '=';
  }
  return String(out);
}

String base64::encode(const String& text) {
  return encode(reinterpret_cast<const uint8_t*>(text.c_str()), text.length());
}

String base64::decode(const String& text) {
  std::string out;
  out.reserve((text.length() / 4) * 3);

  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text.charAt(i);
    if (c == '=') {
      break;
    }
    const int v = decodeChar(c);
    if (v < 0) {
      continue;  // whitespace and line breaks are skipped, as every decoder does
    }
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((acc >> bits) & 0xFF);
    }
  }
  return String(out);
}

// ------------------------------------------------------------------- MD5 ---

namespace {

// RFC 1321. Per-round shift amounts and the sine-derived constant table.
constexpr uint32_t kShift[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

constexpr uint32_t kSine[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

uint32_t rotl(const uint32_t x, const uint32_t c) { return (x << c) | (x >> (32 - c)); }

void transform(uint32_t state[4], const uint8_t block[64]) {
  uint32_t m[16];
  for (int i = 0; i < 16; ++i) {
    // Little-endian, per the spec.
    m[i] = static_cast<uint32_t>(block[i * 4]) | (static_cast<uint32_t>(block[i * 4 + 1]) << 8) |
           (static_cast<uint32_t>(block[i * 4 + 2]) << 16) | (static_cast<uint32_t>(block[i * 4 + 3]) << 24);
  }

  uint32_t a = state[0];
  uint32_t b = state[1];
  uint32_t c = state[2];
  uint32_t d = state[3];

  for (uint32_t i = 0; i < 64; ++i) {
    uint32_t f = 0;
    uint32_t g = 0;
    if (i < 16) {
      f = (b & c) | (~b & d);
      g = i;
    } else if (i < 32) {
      f = (d & b) | (~d & c);
      g = (5 * i + 1) % 16;
    } else if (i < 48) {
      f = b ^ c ^ d;
      g = (3 * i + 5) % 16;
    } else {
      f = c ^ (b | ~d);
      g = (7 * i) % 16;
    }
    const uint32_t tmp = d;
    d = c;
    c = b;
    b = b + rotl(a + f + kSine[i] + m[g], kShift[i]);
    a = tmp;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
}

}  // namespace

void MD5Builder::begin() {
  state[0] = 0x67452301;
  state[1] = 0xefcdab89;
  state[2] = 0x98badcfe;
  state[3] = 0x10325476;
  bitCount = 0;
  bufferLen = 0;
  std::memset(digest, 0, sizeof(digest));
}

void MD5Builder::add(const uint8_t* data, const uint16_t len) {
  if (data == nullptr) {
    return;
  }
  bitCount += static_cast<uint64_t>(len) * 8;
  size_t offset = 0;
  while (offset < len) {
    const size_t room = 64 - bufferLen;
    const size_t take = (len - offset) < room ? (len - offset) : room;
    std::memcpy(buffer + bufferLen, data + offset, take);
    bufferLen += take;
    offset += take;
    if (bufferLen == 64) {
      transform(state, buffer);
      bufferLen = 0;
    }
  }
}

void MD5Builder::add(const char* text) {
  if (text != nullptr) {
    add(reinterpret_cast<const uint8_t*>(text), static_cast<uint16_t>(std::strlen(text)));
  }
}

void MD5Builder::add(const String& text) {
  add(reinterpret_cast<const uint8_t*>(text.c_str()), static_cast<uint16_t>(text.length()));
}

void MD5Builder::calculate() {
  // Pad with 0x80 then zeros, leaving room for the 8-byte length.
  const uint64_t finalBits = bitCount;
  uint8_t pad = 0x80;
  add(&pad, 1);
  bitCount = finalBits;  // padding does not count toward the length
  pad = 0x00;
  while (bufferLen != 56) {
    add(&pad, 1);
    bitCount = finalBits;
  }

  uint8_t lengthBytes[8];
  for (int i = 0; i < 8; ++i) {
    lengthBytes[i] = static_cast<uint8_t>((finalBits >> (8 * i)) & 0xFF);
  }
  std::memcpy(buffer + 56, lengthBytes, 8);
  transform(state, buffer);
  bufferLen = 0;

  for (int i = 0; i < 4; ++i) {
    digest[i * 4] = static_cast<uint8_t>(state[i] & 0xFF);
    digest[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 8) & 0xFF);
    digest[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 16) & 0xFF);
    digest[i * 4 + 3] = static_cast<uint8_t>((state[i] >> 24) & 0xFF);
  }
}

String MD5Builder::toString() {
  char out[33];
  for (int i = 0; i < 16; ++i) {
    std::snprintf(out + i * 2, 3, "%02x", digest[i]);
  }
  return String(out);
}

void MD5Builder::getBytes(uint8_t* output) {
  if (output != nullptr) {
    std::memcpy(output, digest, sizeof(digest));
  }
}

// ------------------------------------------------------------------ SHA-1 ---

namespace {

uint32_t rotl32(const uint32_t x, const uint32_t c) { return (x << c) | (x >> (32 - c)); }

}  // namespace

void sha1(const uint8_t* data, const size_t length, uint8_t digest[20]) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

  // Message plus 0x80, zero padding to 56 mod 64, then the length in bits as
  // a big-endian 64-bit value. Building it in one buffer keeps the block loop
  // simple; the inputs here are handshake keys, never large.
  const size_t totalLen = ((length + 8) / 64 + 1) * 64;
  auto* msg = static_cast<uint8_t*>(std::calloc(totalLen, 1));
  if (msg == nullptr) {
    std::memset(digest, 0, 20);
    return;
  }
  std::memcpy(msg, data, length);
  msg[length] = 0x80;
  const uint64_t bitLen = static_cast<uint64_t>(length) * 8;
  for (int i = 0; i < 8; ++i) {
    msg[totalLen - 1 - i] = static_cast<uint8_t>((bitLen >> (8 * i)) & 0xFF);
  }

  for (size_t off = 0; off < totalLen; off += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      // Big-endian, unlike MD5.
      w[i] = (static_cast<uint32_t>(msg[off + i * 4]) << 24) | (static_cast<uint32_t>(msg[off + i * 4 + 1]) << 16) |
             (static_cast<uint32_t>(msg[off + i * 4 + 2]) << 8) | static_cast<uint32_t>(msg[off + i * 4 + 3]);
    }
    for (int i = 16; i < 80; ++i) {
      w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = h[0];
    uint32_t b = h[1];
    uint32_t c = h[2];
    uint32_t d = h[3];
    uint32_t e = h[4];

    for (int i = 0; i < 80; ++i) {
      uint32_t f = 0;
      uint32_t k = 0;
      if (i < 20) {
        f = (b & c) | (~b & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      const uint32_t tmp = rotl32(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rotl32(b, 30);
      b = a;
      a = tmp;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }

  std::free(msg);

  for (int i = 0; i < 5; ++i) {
    digest[i * 4] = static_cast<uint8_t>((h[i] >> 24) & 0xFF);
    digest[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFF);
    digest[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFF);
    digest[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFF);
  }
}

// ------------------------------------------------------------ mbedtls ---
//
// Forwarded to the implementations above rather than stubbed: these encode and
// decode stored credentials, and a wrong answer corrupts them silently.

#include "arduino-shim/mbedtls/base64.h"

int mbedtls_base64_encode(unsigned char* dst, const size_t dlen, size_t* olen, const unsigned char* src,
                          const size_t slen) {
  const String encoded = base64::encode(src, slen);
  const size_t needed = encoded.length() + 1;  // mbedtls counts the NUL
  if (olen != nullptr) {
    *olen = needed;
  }
  // mbedtls's documented probe: a null destination asks only for the size.
  if (dst == nullptr || dlen < needed) {
    return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
  }
  std::memcpy(dst, encoded.c_str(), needed);
  return 0;
}

int mbedtls_base64_decode(unsigned char* dst, const size_t dlen, size_t* olen, const unsigned char* src,
                          const size_t slen) {
  const String decoded = base64::decode(String(std::string(reinterpret_cast<const char*>(src), slen)));
  if (olen != nullptr) {
    *olen = decoded.length();
  }
  if (dst == nullptr || dlen < decoded.length()) {
    return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
  }
  std::memcpy(dst, decoded.c_str(), decoded.length());
  return 0;
}
