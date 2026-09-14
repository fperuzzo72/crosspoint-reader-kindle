#pragma once

// Stand-in for the Arduino core's umbrella header on the Kindle build.
//
// The tree is full of `#include <Arduino.h>`, including inside the FreeInk
// SDK. Rather than touch any of those, this directory goes on the include
// path ahead of nothing at all, and the include resolves here. That keeps the
// port's diff against upstream to files it genuinely owns.
//
// Deliberately thin: it forwards to ArduinoCompat.h, whose contents are scoped
// by what the tree measurably uses. If something is missing, the compiler says
// so at the real call site, which is the outcome we want.

#include "../ArduinoCompat.h"
// The real core's umbrella header brings Serial with it, and 24 files in the
// tree rely on that rather than including HardwareSerial.h themselves.
// The ESP32 Arduino core builds on FreeRTOS and exposes it through this
// header; code written against that core calls vTaskDelay without including
// anything. Dropping it would break files that never named FreeRTOS at all.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "HardwareSerial.h"
#include "Print.h"
#include "Stream.h"

// The core pulls these in for its users; code written against it assumes they
// are already there.
// The real core's umbrella header drags most of the C library in with it, and
// the tree leans on that: fabsf, assert, va_start and round are all used
// without their own includes.
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// The tree uses these without their own includes, the way the real core lets
// it: <algorithm> for any_of/find_if, <string> for std::string in signatures.
#include <algorithm>
#include <string>

// Arduino spells these in its own vocabulary. A handful of SDK headers use
// them in signatures, so they have to exist even where nothing calls them.
using boolean = bool;
using byte = uint8_t;
using word = uint16_t;

#ifndef HIGH
#define HIGH 1
#define LOW 0
#endif

#ifndef INPUT
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define OUTPUT_OPEN_DRAIN 4
#endif

// PROGMEM is an AVR storage qualifier that the ESP32 core already reduces to
// nothing. On a Linux target it is nothing too, and the pgm_read_* accessors
// are plain dereferences.
#ifndef PROGMEM
#define PROGMEM
#endif

// ESP32 section attributes. They decide which memory a symbol lands in, which
// matters on a chip with separate instruction RAM and cached flash. A Linux
// process has one address space, so they are nothing.
#ifndef IRAM_ATTR
#define IRAM_ATTR
#define DRAM_ATTR
#define RTC_DATA_ATTR
#define RTC_NOINIT_ATTR
#define EXT_RAM_ATTR
#endif
#define pgm_read_byte(addr) (*reinterpret_cast<const uint8_t*>(addr))
#define pgm_read_word(addr) (*reinterpret_cast<const uint16_t*>(addr))
#define pgm_read_dword(addr) (*reinterpret_cast<const uint32_t*>(addr))
#define pgm_read_ptr(addr) (*reinterpret_cast<void* const*>(addr))

// The _P string and memory family. On AVR these read from a separate program
// address space; the ESP32 core already reduces them to the ordinary calls,
// and so does this. PNGdec and JPEGDEC use memcpy_P heavily.
#define memcpy_P memcpy
#define memcmp_P memcmp
#define strcpy_P strcpy
#define strncpy_P strncpy
#define strcmp_P strcmp
#define strncmp_P strncmp
#define strlen_P strlen
#define strstr_P strstr
#define sprintf_P sprintf
#define snprintf_P snprintf
#define PSTR(s) (s)
#define F(s) (s)

#ifndef min
template <typename A, typename B>
constexpr auto min(A a, B b) -> decltype(a < b ? a : b) {
  return a < b ? a : b;
}
template <typename A, typename B>
constexpr auto max(A a, B b) -> decltype(a > b ? a : b) {
  return a > b ? a : b;
}
#endif

constexpr long constrainValue(const long v, const long lo, const long hi) { return v < lo ? lo : (v > hi ? hi : v); }
#define constrain(v, lo, hi) constrainValue((v), (lo), (hi))

// GPIO. The KT3 has no buttons CrossPoint can read and no panel pins to
// bit-bang: the kernel owns all of it. These exist so the handful of call
// sites still link, and they do nothing on purpose.
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int digitalRead(uint8_t) { return LOW; }
inline int analogRead(uint8_t) { return 0; }

// Arduino's random() takes a bound, unlike the C library's no-argument one.
// These are overloads alongside it rather than a redefinition; without them a
// random(a, b) resolves to the C function and fails on the argument count.
inline long random(const long maxExclusive) {
  return maxExclusive > 0 ? static_cast<long>(::rand()) % maxExclusive : 0;
}
inline long random(const long minInclusive, const long maxExclusive) {
  return maxExclusive > minInclusive ? minInclusive + random(maxExclusive - minInclusive) : minInclusive;
}
inline void randomSeed(const unsigned long seed) { ::srand(static_cast<unsigned>(seed)); }
inline uint32_t analogReadMilliVolts(uint8_t) { return 0; }

// Clock speed. Reported as a plausible constant because callers use it for
// diagnostics and for scaling busy-waits, not for timing correctness; the i.MX6
// in this device runs at 1 GHz.
inline uint32_t getCpuFrequencyMhz() { return 1000; }

// BSD string copies. glibc does not ship them; the ESP32 core does, and the
// tree uses them for their truncation guarantee. Reimplemented rather than
// mapped onto strncpy, which does NOT guarantee termination.
inline size_t strlcpy(char* dst, const char* src, const size_t size) {
  const size_t srcLen = std::strlen(src);
  if (size != 0) {
    const size_t copy = srcLen < size - 1 ? srcLen : size - 1;
    std::memcpy(dst, src, copy);
    dst[copy] = '\0';
  }
  return srcLen;  // the length it TRIED to create, so callers can detect truncation
}

inline size_t strlcat(char* dst, const char* src, const size_t size) {
  const size_t dstLen = std::strlen(dst);
  if (dstLen >= size) {
    return size + std::strlen(src);
  }
  return dstLen + strlcpy(dst + dstLen, src, size - dstLen);
}
inline bool setCpuFrequencyMhz(uint32_t) { return false; }
inline void analogReadResolution(uint8_t) {}

// Timezone configuration goes through the system here; see esp_sntp.h.
inline void configTzTime(const char*, const char*, const char* = nullptr, const char* = nullptr) {}
inline void configTime(long, int, const char*, const char* = nullptr, const char* = nullptr) {}
