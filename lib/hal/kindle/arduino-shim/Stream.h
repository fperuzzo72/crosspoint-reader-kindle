#pragma once

// Arduino's Stream: Print plus the ability to read back.
//
// Nothing in this port feeds an input stream yet, so the reading half has
// honest do-nothing defaults rather than being left abstract, which would make
// every subclass in the tree fail to instantiate.

#include "Print.h"

class Stream : public Print {
 public:
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  virtual int peek() { return -1; }
  void setTimeout(unsigned long ms) { timeoutMs = ms; }
  unsigned long getTimeout() const { return timeoutMs; }

 protected:
  unsigned long timeoutMs = 1000;
};
