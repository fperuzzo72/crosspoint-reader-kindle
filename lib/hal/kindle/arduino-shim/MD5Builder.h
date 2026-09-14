#pragma once

// MD5, implemented rather than stubbed.
//
// KOReader's sync protocol identifies a document by the MD5 of a sample of its
// bytes. A stub returning a constant would make every book the same document
// and quietly cross-contaminate reading positions between them, which is the
// kind of failure that destroys data rather than merely not working.
//
// Not a security primitive here and not used as one: this is content
// addressing, and the protocol on the other end specifies MD5.

#include <cstddef>
#include <cstdint>

#include "../ArduinoCompat.h"

class MD5Builder {
 public:
  void begin();
  void add(const uint8_t* data, uint16_t len);
  void add(const char* text);
  void add(const String& text);
  void calculate();
  String toString();
  void getBytes(uint8_t* output);  // 16 bytes

 private:
  uint32_t state[4] = {0};
  uint64_t bitCount = 0;
  uint8_t buffer[64] = {0};
  size_t bufferLen = 0;
  uint8_t digest[16] = {0};
};
