#pragma once

// SPI stand-in.
//
// 33 files include this, all of them reaching for a bus that does not exist on
// this target: the Kindle's panel and storage are the kernel's, and nothing
// here bit-bangs a peripheral. The calls are shaped like Arduino's and do
// nothing.
//
// These are NOT a working SPI. Any code that genuinely needs to move bytes
// over a bus will appear to succeed and transfer zeros, so a device profile
// that needs real SPI must replace this rather than lean on it.

#include <cstdint>

#ifndef SPI_MODE0
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3
#endif
#ifndef MSBFIRST
#define MSBFIRST 1
#define LSBFIRST 0
#endif

class SPISettings {
 public:
  SPISettings() = default;
  SPISettings(uint32_t, uint8_t, uint8_t) {}
};

class SPIClass {
 public:
  SPIClass() = default;
  explicit SPIClass(uint8_t) {}
  void begin(int8_t = -1, int8_t = -1, int8_t = -1, int8_t = -1) {}
  void end() {}
  void beginTransaction(SPISettings) {}
  void endTransaction() {}
  void setFrequency(uint32_t) {}
  void setDataMode(uint8_t) {}
  void setBitOrder(uint8_t) {}
  uint8_t transfer(uint8_t) { return 0; }
  uint16_t transfer16(uint16_t) { return 0; }
  void transfer(void*, size_t) {}
  void writeBytes(const uint8_t*, uint32_t) {}
};

extern SPIClass SPI;
