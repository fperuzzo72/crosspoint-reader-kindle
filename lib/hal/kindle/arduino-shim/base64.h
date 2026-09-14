#pragma once

// Base64, implemented rather than stubbed.
//
// The callers are HTTP Basic auth and the obfuscation used for stored
// credentials. A stub would produce a string that looks like base64 and
// authenticates as nobody, which fails far from the cause.

#include <cstddef>

#include "../ArduinoCompat.h"

class base64 {
 public:
  static String encode(const uint8_t* data, size_t length);
  static String encode(const String& text);
  static String decode(const String& text);
};

// SHA-1, needed for the WebSocket handshake (RFC 6455 hashes the client key
// with a fixed GUID and returns it base64'd). Lives here rather than in its own
// header because base64 is its only caller and the two are used together.
//
// Not a security primitive and not used as one: the handshake is a protocol
// formality, and SHA-1's collision weakness is irrelevant to it.
void sha1(const uint8_t* data, size_t length, uint8_t digest[20]);
