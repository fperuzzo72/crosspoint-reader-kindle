#pragma once

// Arduino's IPAddress, a plain value type. Nothing device-specific about it,
// so this is a straight reimplementation rather than a stub.

#include <cstdint>
#include <cstdio>

#include "../ArduinoCompat.h"

class IPAddress {
 public:
  IPAddress() = default;
  IPAddress(const uint8_t a, const uint8_t b, const uint8_t c, const uint8_t d) : octets{a, b, c, d} {}
  // Host byte order in, matching Arduino.
  explicit IPAddress(const uint32_t addr) {
    octets[0] = static_cast<uint8_t>(addr & 0xFF);
    octets[1] = static_cast<uint8_t>((addr >> 8) & 0xFF);
    octets[2] = static_cast<uint8_t>((addr >> 16) & 0xFF);
    octets[3] = static_cast<uint8_t>((addr >> 24) & 0xFF);
  }

  uint8_t operator[](const int i) const { return octets[i & 3]; }
  uint8_t& operator[](const int i) { return octets[i & 3]; }

  operator uint32_t() const {
    return static_cast<uint32_t>(octets[0]) | (static_cast<uint32_t>(octets[1]) << 8) |
           (static_cast<uint32_t>(octets[2]) << 16) | (static_cast<uint32_t>(octets[3]) << 24);
  }

  bool operator==(const IPAddress& o) const {
    return octets[0] == o.octets[0] && octets[1] == o.octets[1] && octets[2] == o.octets[2] &&
           octets[3] == o.octets[3];
  }
  bool operator!=(const IPAddress& o) const { return !(*this == o); }

  // The tree uses both spellings.
  String toString() const;

  // Callers format this into log lines and QR payloads.
  size_t toCharArray(char* buf, const size_t len) const {
    return static_cast<size_t>(std::snprintf(buf, len, "%u.%u.%u.%u", octets[0], octets[1], octets[2], octets[3]));
  }

 private:
  uint8_t octets[4] = {0, 0, 0, 0};
};

#ifndef INADDR_NONE_DEFINED
#define INADDR_NONE_DEFINED
inline const IPAddress INADDR_NONE_IP(0, 0, 0, 0);
#endif
