#pragma once

// Arduino compatibility for the Kindle (POSIX) build.
//
// The app tree is written against the Arduino core. Rewriting 176 String call
// sites and 153 millis() ones to be platform-neutral would be a large, noisy,
// regression-prone diff across code this port otherwise does not need to
// touch, and it would fork this tree away from upstream for no benefit. A
// shim is the cheaper and far more reviewable trade.
//
// Its scope is set by measurement, not by reimplementing the Arduino core.
// What the tree actually uses:
//
//   String          176 sites, 15 distinct methods
//   millis/micros   162      delay 46      yield 55
//   ESP.*            95      getFreeHeap, getMaxAllocHeap, restart, nothing else
//
// Anything outside that set is deliberately absent: a missing symbol is a
// compile error that points at the real call site, which is what you want,
// whereas a stub that silently does the wrong thing at runtime is not.

#include <cstdint>
#include <cstring>
#include <string>

// ---------------------------------------------------------------- String ---
//
// Arduino's String over std::string. The semantics that differ from
// std::string are the ones that matter here: indexOf returns -1 rather than
// npos, substring takes [from, to) with clamping instead of throwing, and
// remove() is an in-place erase that tolerates out-of-range indices. Code
// written against Arduino relies on that forgiveness.
class String {
 public:
  String() = default;
  String(const char* s) : buf(s != nullptr ? s : "") {}
  // Explicit on purpose: an implicit conversion here made calls such as
  // hasEpubExtension(std::string) ambiguous between the String and const char*
  // overloads. Arduino's String has no std::string constructor at all, so
  // requiring the cast costs the tree nothing and matches upstream.
  explicit String(const std::string& s) : buf(s) {}
  // Not explicit: Arduino's String converts from char implicitly and callers
  // pass a bare character where a String is expected.
  String(char c) : buf(1, c) {}
  explicit String(int v) : buf(std::to_string(v)) {}
  explicit String(unsigned v) : buf(std::to_string(v)) {}
  explicit String(long v) : buf(std::to_string(v)) {}
  explicit String(unsigned long v) : buf(std::to_string(v)) {}

  const char* c_str() const { return buf.c_str(); }
  size_t length() const { return buf.length(); }
  bool isEmpty() const { return buf.empty(); }
  void reserve(size_t n) { buf.reserve(n); }
  char charAt(size_t i) const { return i < buf.size() ? buf[i] : '\0'; }
  char operator[](size_t i) const { return charAt(i); }

  // Arduino returns -1 for "not found"; std::string returns npos.
  int indexOf(const String& needle, size_t from = 0) const;
  int indexOf(char needle, size_t from = 0) const;
  int lastIndexOf(const String& needle) const;
  int lastIndexOf(char needle) const;

  bool startsWith(const String& prefix) const;
  bool endsWith(const String& suffix) const;
  bool equals(const String& other) const { return buf == other.buf; }
  bool equalsIgnoreCase(const String& other) const;

  // [from, to), both clamped to length. Never throws.
  String substring(size_t from) const;
  String substring(size_t from, size_t to) const;

  // In-place erase, out-of-range tolerant.
  void remove(size_t index);
  void remove(size_t index, size_t count);

  void replace(const String& from, const String& to);
  void trim();
  void toUpperCase();
  void toLowerCase();
  long toInt() const;
  double toFloat() const;

  // ArduinoJson's writer appends through write(); the real Arduino String is
  // special-cased inside the library, and this one is not, so it takes the
  // generic path and needs these.
  size_t write(const char* s, size_t n) {
    if (s == nullptr) {
      return 0;
    }
    buf.append(s, n);
    return n;
  }
  size_t write(const uint8_t* s, size_t n) { return write(reinterpret_cast<const char*>(s), n); }
  size_t write(uint8_t c) {
    buf += static_cast<char>(c);
    return 1;
  }
  // Arduino's String::concat family. ArduinoJson's ::String support calls
  // these directly when it builds a document into one, so the set has to match
  // what it expects rather than just what the app tree uses.
  bool concat(const char* s, size_t n) { return write(s, n) == n; }
  bool concat(const char* s) { return s == nullptr ? false : concat(s, std::strlen(s)); }
  bool concat(const String& s) { return concat(s.c_str(), s.length()); }
  bool concat(char c) {
    buf += c;
    return true;
  }
  bool concat(unsigned char c) { return concat(static_cast<char>(c)); }
  bool concat(int v) { return concat(std::to_string(v).c_str()); }
  bool concat(unsigned v) { return concat(std::to_string(v).c_str()); }
  bool concat(long v) { return concat(std::to_string(v).c_str()); }
  bool concat(unsigned long v) { return concat(std::to_string(v).c_str()); }
  bool concat(double v) { return concat(std::to_string(v).c_str()); }

  // ArduinoJson's Reader<String> pulls characters out one at a time. The real
  // Arduino String is special-cased inside the library; this one takes the
  // generic path, which expects a stream-like read().
  int read() const { return readPos < buf.size() ? static_cast<uint8_t>(buf[readPos++]) : -1; }
  size_t readBytes(char* out, size_t n) const {
    const size_t left = buf.size() - readPos;
    const size_t take = n < left ? n : left;
    std::memcpy(out, buf.data() + readPos, take);
    readPos += take;
    return take;
  }

  const std::string& str() const { return buf; }

  String& operator+=(const String& rhs) {
    buf += rhs.buf;
    return *this;
  }
  String& operator+=(const char* rhs) {
    if (rhs != nullptr) {
      buf += rhs;
    }
    return *this;
  }
  String& operator+=(char rhs) {
    buf += rhs;
    return *this;
  }

  friend String operator+(String lhs, const String& rhs) { return lhs += rhs; }
  friend String operator+(String lhs, const char* rhs) { return lhs += rhs; }
  friend String operator+(String lhs, char rhs) { return lhs += rhs; }

  friend bool operator==(const String& a, const String& b) { return a.buf == b.buf; }
  friend bool operator!=(const String& a, const String& b) { return a.buf != b.buf; }
  friend bool operator<(const String& a, const String& b) { return a.buf < b.buf; }

 private:
  std::string buf;
  // Mutable because ArduinoJson's reader takes a const String& and still
  // consumes characters from it; the logical value is unchanged by reading.
  mutable size_t readPos = 0;
};

// ---------------------------------------------------------------- timing ---
//
// Zeroed at first use, so millis() counts from process start the way the
// Arduino core counts from boot. CLOCK_MONOTONIC, so a clock adjustment (the
// Kindle does talk to Amazon's time servers) cannot make time run backwards
// under code that assumes it never does.
uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);

// A no-op. On the ESP32 this feeds the watchdog and lets other tasks run; a
// Linux process is preempted whether it cooperates or not. Kept as a real
// symbol rather than a macro so the 55 call sites stay untouched.
inline void yield() {}

// ------------------------------------------------------------------- ESP ---
class EspClass {
 public:
  // Free RAM as the kernel sees it. The ESP32 answers for its own heap; here
  // the number is system-wide, so callers using it to decide whether a big
  // allocation will succeed get a looser answer, not a wrong one.
  uint32_t getFreeHeap() const;
  // On the ESP32 this is the largest single contiguous block, which differs
  // sharply from the total because that heap fragments. Linux hands out
  // virtual address space, so the distinction largely dissolves.
  uint32_t getMaxAllocHeap() const;
  // Restarts the reader, NOT the device: on the ESP32 this reboots the chip,
  // which is the same thing because the firmware IS the device. Here it
  // re-execs the process. Rebooting a Kindle to restart an app would be
  // wrong, and would take a minute.
  [[noreturn]] void restart() const;
};

// ArduinoJson needs no custom converters here: it special-cases a class named
// String by name, and this one is. Writing converters anyway made every
// conversion ambiguous against the ones it already had.
//
// What it DOES need is ARDUINOJSON_ENABLE_ARDUINO_STRING=1, passed by the
// build: the library auto-detects Arduino by probing for the real core, which
// this shim is not, so the support has to be asked for explicitly.

extern EspClass ESP;

// Serial now lives in arduino-shim/HardwareSerial.h, on top of Arduino's
// Print hierarchy, because 59 files in the tree want Print.h and deriving from
// it is what makes Serial fit the rest of them.

