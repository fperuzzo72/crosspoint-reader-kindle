#pragma once

// Arduino's network client interface. Sockets are not wired up in this port
// yet, so this is the shape without the substance: connect() fails rather than
// pretending, which surfaces at the call site instead of hanging.

#include "IPAddress.h"
#include "Stream.h"

class Client : public Stream {
 public:
  virtual int connect(const char* host, uint16_t port) = 0;
  // SecureClient marks both as override, so both must be virtual here.
  virtual int connect(IPAddress ip, uint16_t port) = 0;
  virtual void stop() = 0;
  virtual uint8_t connected() = 0;
  // SecureClient marks this override, so the base has to declare it virtual.
  virtual explicit operator bool() { return connected() != 0; }
};
