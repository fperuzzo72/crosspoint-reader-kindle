#pragma once

// Arduino's Preferences, which is NVS underneath.
//
// Reports failure rather than storing anywhere, because CrossPoint's own
// settings already go through HalStorage to files on the card; a second,
// silently-empty store would be a place for settings to vanish into.

#include <cstddef>

#include "../ArduinoCompat.h"

class Preferences {
 public:
  bool begin(const char*, bool = false) { return false; }
  void end() {}
  bool clear() { return false; }
  bool remove(const char*) { return false; }
  size_t putString(const char*, const String&) { return 0; }
  String getString(const char*, const String& defaultValue = String()) { return defaultValue; }
  size_t putUInt(const char*, uint32_t) { return 0; }
  uint32_t getUInt(const char*, uint32_t defaultValue = 0) { return defaultValue; }
  size_t putBool(const char*, bool) { return 0; }
  bool getBool(const char*, bool defaultValue = false) { return defaultValue; }
  size_t putUChar(const char*, uint8_t) { return 0; }
  uint8_t getUChar(const char*, uint8_t defaultValue = 0) { return defaultValue; }
  size_t putInt(const char*, int32_t) { return 0; }
  int32_t getInt(const char*, int32_t defaultValue = 0) { return defaultValue; }
  size_t putBytes(const char*, const void*, size_t) { return 0; }
  size_t getBytes(const char*, void*, size_t) { return 0; }
  size_t getBytesLength(const char*) { return 0; }
  bool isKey(const char*) { return false; }
};
