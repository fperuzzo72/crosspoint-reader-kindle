#pragma once

// Serial for the Kindle build.
//
// There is no UART worth talking to here. The useful destination for a
// diagnostic is stderr, which the launching scriptlet already redirects into a
// log on the card, so that is where this goes.

#include "Print.h"
#include "Stream.h"

class HardwareSerial : public Stream {
 public:
  void begin(unsigned long = 0, uint32_t = 0, int8_t = -1, int8_t = -1) {}
  void end() {}
  // Not explicit: the tree writes `return Serial;` where a bool is wanted.
  operator bool() const { return true; }

  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  void flush() override;
};

extern HardwareSerial Serial;
// Sticky selects Serial0 by name; alias it so that branch compiles too.
extern HardwareSerial Serial0;
