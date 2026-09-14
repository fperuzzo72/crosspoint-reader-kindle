#pragma once

// Arduino's OTA updater.
//
// On the ESP32 this writes a new firmware image into the inactive flash slot
// and flips the boot partition. None of that exists here: this build is a file
// on a filesystem, updated by copying a new one over it, and there is no slot
// to flip.
//
// Every call therefore reports failure. That is deliberate rather than lazy:
// an updater that claimed to have written an image would leave the user
// believing they had updated when nothing happened, and the next boot would
// silently run the old binary. Failing is the honest outcome, and the UI
// already has a path for "update failed".
//
// If a Kindle update mechanism is ever wanted, it belongs in a Kindle-shaped
// implementation (download, verify, replace the file, re-exec), not in a
// pretend flash writer.

#include <cstddef>
#include <cstdint>

#include "../ArduinoCompat.h"
#include "Stream.h"

#define OTA_SIZE_UNKNOWN 0xFFFFFFFF
#define U_FLASH 0
#define U_SPIFFS 100

#define UPDATE_ERROR_OK 0
#define UPDATE_ERROR_SPACE 2
#define UPDATE_ERROR_NO_PARTITION 8

class UpdateClass {
 public:
  bool begin(size_t = OTA_SIZE_UNKNOWN, int = U_FLASH, int = -1, uint8_t = 0) { return false; }
  size_t write(uint8_t*, size_t) { return 0; }
  size_t writeStream(Stream&) { return 0; }
  bool end(bool = false) { return false; }
  void abort() {}
  bool isFinished() const { return false; }
  bool hasError() const { return true; }
  uint8_t getError() const { return UPDATE_ERROR_NO_PARTITION; }
  void clearError() {}
  const char* errorString() const { return "OTA does not apply on this target"; }
  size_t size() const { return 0; }
  size_t progress() const { return 0; }
  size_t remaining() const { return 0; }
  void onProgress(void (*)(size_t, size_t)) {}
  bool setMD5(const char*) { return false; }
  String md5String() { return String(); }
};

extern UpdateClass Update;
