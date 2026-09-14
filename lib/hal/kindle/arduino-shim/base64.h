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
