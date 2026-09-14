#pragma once

// Arduino's Stream: Print plus the ability to read back.
//
// Nothing in this port feeds an input stream yet, so the reading half has
// honest do-nothing defaults rather than being left abstract, which would make
// every subclass in the tree fail to instantiate.

#include "../ArduinoCompat.h"
#include "Print.h"

class Stream : public Print {
 public:
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  // SecureClient marks the buffered form as override, so it must be virtual
  // here rather than only existing on the concrete socket classes.
  virtual int read(uint8_t* buf, size_t size) {
    (void)buf;
    (void)size;
    return -1;
  }
  virtual int peek() { return -1; }
  // Reads until the terminator or the timeout. Nothing feeds an input stream
  // on this target, so the default read() returns -1 and this returns empty
  // immediately rather than blocking for the timeout on every call.
  String readStringUntil(char terminator);
  String readString();

  void setTimeout(unsigned long ms) { timeoutMs = ms; }
  unsigned long getTimeout() const { return timeoutMs; }

 protected:
  unsigned long timeoutMs = 1000;
};
