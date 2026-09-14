#pragma once

// I2C stand-in. Same reasoning as SPI.h: the three files that include this are
// after a fuel gauge, an RTC and an IMU, none of which this port reaches. Reads
// return nothing and writes go nowhere.

#include <cstdint>

class TwoWire {
 public:
  explicit TwoWire(uint8_t = 0) {}
  bool begin(int = -1, int = -1, uint32_t = 0) { return false; }
  void end() {}
  void setClock(uint32_t) {}
  void setTimeOut(uint16_t) {}
  uint16_t getTimeOut() { return 0; }
  void beginTransmission(uint8_t) {}
  uint8_t endTransmission(bool = true) { return 2; }  // 2 = NACK on address
  uint8_t requestFrom(uint8_t, uint8_t, bool = true) { return 0; }
  size_t write(uint8_t) { return 0; }
  size_t write(const uint8_t*, size_t) { return 0; }
  int available() { return 0; }
  int read() { return -1; }
};

extern TwoWire Wire;
