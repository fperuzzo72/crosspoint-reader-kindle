#pragma once
// MAC addresses come from the interface, not from efuses. WiFi.macAddress()
// reads the real one through SIOCGIFHWADDR; this exists for callers that ask
// ESP-IDF directly, and tells them it cannot help.
#include <cstdint>
inline int esp_read_mac(uint8_t*, int) { return -1; }
inline int esp_efuse_mac_get_default(uint8_t*) { return -1; }
