#pragma once

// Wi-Fi on a Kindle, where the radio is not ours.
//
// Every other target runs FreeInk as the firmware, so WiFi.begin() genuinely
// owns the radio. Here the device is a Kindle running Amazon's stack, which
// associates, roams, sleeps the radio and reconnects on its own. A reader
// process that called begin() or softAP() would be fighting the system, and
// the user would lose their connection.
//
// So the split is deliberate and it is the whole design of this file:
//
//   Reading the truth is REAL. localIP, macAddress, RSSI and status come from
//   the actual interface, via getifaddrs and /proc/net/wireless.
//
//   Taking CONTROL fails honestly. begin, softAP, disconnect and scanning
//   return failure rather than pretending to have worked. A caller that
//   believes a fake success will wait forever for an association that nobody
//   asked for.
//
// The practical consequence for CrossPoint: the "join a network" and "host a
// hotspot" flows do not apply on this target and their UI should be hidden by
// capability, not left to fail at the button. Everything downstream that just
// wants a socket works, because sockets are real.

#include <cstdint>

#include "IPAddress.h"
#include "WString.h"

// Arduino's wl_status_t.
enum wl_status_t : uint8_t {
  WL_NO_SHIELD = 255,
  WL_IDLE_STATUS = 0,
  WL_NO_SSID_AVAIL = 1,
  WL_SCAN_COMPLETED = 2,
  WL_CONNECTED = 3,
  WL_CONNECT_FAILED = 4,
  WL_CONNECTION_LOST = 5,
  WL_DISCONNECTED = 6,
};

using wifi_mode_t = uint8_t;
#define WIFI_OFF 0
#define WIFI_STA 1
#define WIFI_AP 2
#define WIFI_AP_STA 3
// ESP-IDF's spellings of the same set; the tree uses both.
#define WIFI_MODE_NULL 0
#define WIFI_MODE_STA 1
#define WIFI_MODE_AP 2
#define WIFI_MODE_APSTA 3

// Arduino's scan-state sentinels. Scanning is not available here, so
// scanComplete() always reports FAILED rather than leaving a caller polling
// RUNNING forever.
// Scan strategy selectors. Scanning is unavailable here, so these only exist
// for the setScanMethod/setSortMethod calls that pass them.
#define WIFI_FAST_SCAN 0
#define WIFI_ALL_CHANNEL_SCAN 1
#define WIFI_CONNECT_AP_BY_SIGNAL 0
#define WIFI_CONNECT_AP_BY_SECURITY 1

#define WIFI_SCAN_RUNNING (-1)
#define WIFI_SCAN_FAILED (-2)

#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA2_PSK 3

class WiFiClass {
 public:
  // --- real: read from the system -----------------------------------------
  // WL_CONNECTED when an interface other than loopback has an IPv4 address.
  wl_status_t status();
  IPAddress localIP();
  String macAddress();
  // dBm, parsed from /proc/net/wireless. 0 when unavailable.
  int32_t RSSI();
  String SSID();
  String getHostname();

  // --- not ours: fail rather than pretend ---------------------------------
  // The system owns association. Returns WL_CONNECT_FAILED without touching
  // anything; a caller that treated a fake success as real would block on a
  // connection nobody requested.
  wl_status_t begin(const char* ssid = nullptr, const char* passphrase = nullptr);
  bool disconnect(bool wifiOff = false, bool eraseAp = false);
  // The full ESP32 signature: channel, hidden, max connections. Accepted so
  // the call sites compile unchanged, and refused all the same.
  bool softAP(const char* ssid, const char* passphrase = nullptr, int32_t channel = 1, bool ssidHidden = false,
              int32_t maxConnections = 4);
  bool softAPdisconnect(bool wifiOff = false);
  IPAddress softAPIP() { return IPAddress(); }
  uint8_t softAPgetStationNum() { return 0; }
  int16_t scanNetworks(bool async = false);
  int16_t scanComplete() { return WIFI_SCAN_FAILED; }
  void scanDelete() {}
  String SSID(uint8_t index);  // index unused: scanning is not available here
  int32_t RSSI(uint8_t index);
  // No scan results exist to describe, so the index names nothing.
  uint8_t encryptionType(uint8_t) { return WIFI_AUTH_OPEN; }

  // --- policy knobs with nothing to set ------------------------------------
  // Radio power, reconnection and roaming are the system's. Accepted so the
  // call sites compile; they change nothing.
  bool mode(wifi_mode_t) { return false; }
  wifi_mode_t getMode() { return WIFI_STA; }
  bool setSleep(bool) { return false; }
  void persistent(bool) {}
  bool setAutoReconnect(bool) { return false; }
  bool setHostname(const char*) { return false; }
  void setScanMethod(int) {}
  void setSortMethod(int) {}
};

extern WiFiClass WiFi;
