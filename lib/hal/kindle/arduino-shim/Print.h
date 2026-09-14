#pragma once

// Arduino's Print base class.
//
// 59 files in the tree reach for this, more than any other missing header, and
// it is the root of the Arduino output hierarchy: anything that can emit bytes
// derives from it and inherits the print/println family for free. Reproducing
// it is what lets Serial, the SD writers and the network clients keep their
// existing shapes.
//
// Only write() is pure; everything else is built on it, exactly as upstream
// does, so a subclass only has to supply the one method.

#include <cstddef>
#include <cstdint>
#include <cstdio>

class String;

class Print {
 public:
  virtual ~Print() = default;

  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size);
  size_t write(const char* s);

  size_t print(const char* s);
  // Arduino's Print takes String by const ref, and callers rely on it; without
  // this overload a print(someString) finds no match at all.
  size_t print(const String& s);
  size_t println(const String& s);
  size_t print(char c) { return write(static_cast<uint8_t>(c)); }
  size_t print(int v);
  size_t print(unsigned v);
  size_t print(long v);
  size_t print(unsigned long v);
  size_t print(double v, int digits = 2);

  size_t println();
  size_t println(const char* s);
  size_t println(char c);
  size_t println(int v);
  size_t println(unsigned v);
  size_t println(long v);
  size_t println(unsigned long v);
  size_t println(double v, int digits = 2);

  size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

  virtual void flush() {}
};
