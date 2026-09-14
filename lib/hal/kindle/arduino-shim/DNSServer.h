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

// The reply codes the captive-portal caller names. Nothing here answers a DNS
// query, but the setter takes one of these.
enum class DNSReplyCode {
  NoError = 0,
  FormError = 1,
  ServerFailure = 2,
  NonExistentDomain = 3,
  NotImplemented = 4,
  Refused = 5,
};

class DNSServer {
 public:
  bool start(uint16_t, const String&, const IPAddress&) { return false; }
  void stop() {}
  void processNextRequest() {}
  void setErrorReplyCode(DNSReplyCode) {}
  void setTTL(uint32_t) {}
};
