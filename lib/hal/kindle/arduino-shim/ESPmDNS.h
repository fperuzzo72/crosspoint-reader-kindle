#pragma once

// mDNS service advertisement.
//
// The Kindle runs its own network stack and, on a stock firmware, no
// responder this process may register with. Advertising would need to go
// through the system's daemon, which is not something a reader should be
// reconfiguring.
//
// So begin() fails and says so: callers fall back to showing the IP address,
// which works, rather than printing a .local name that nothing resolves.

#include "../ArduinoCompat.h"
#include "IPAddress.h"

class MDNSResponder {
 public:
  bool begin(const char*) { return false; }
  void end() {}
  void addService(const char*, const char*, uint16_t) {}
  void addServiceTxt(const char*, const char*, const char*, const char*) {}
  int queryService(const char*, const char*) { return 0; }
  String hostname(int) { return String(); }
  IPAddress IP(int) { return IPAddress(); }
  uint16_t port(int) { return 0; }
};

extern MDNSResponder MDNS;
