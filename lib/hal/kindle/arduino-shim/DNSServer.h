#pragma once

// Captive-portal DNS.
//
// Its only purpose is the AP mode that this target cannot enter: the radio
// belongs to the system, softAP() fails, and there is no hotspot for a captive
// portal to capture. start() therefore fails rather than binding port 53 for
// nothing.

#include <cstdint>

#include "../ArduinoCompat.h"
#include "IPAddress.h"

class DNSServer {
 public:
  bool start(uint16_t, const String&, const IPAddress&) { return false; }
  void stop() {}
  void processNextRequest() {}
  void setErrorReplyCode(int) {}
  void setTTL(uint32_t) {}
};
