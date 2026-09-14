#pragma once

// Arduino's network client interface. Sockets are not wired up in this port
// yet, so this is the shape without the substance: connect() fails rather than
// pretending, which surfaces at the call site instead of hanging.

#include "Stream.h"

class Client : public Stream {
 public:
  virtual int connect(const char* host, uint16_t port) = 0;
  virtual void stop() = 0;
  virtual uint8_t connected() = 0;
};
