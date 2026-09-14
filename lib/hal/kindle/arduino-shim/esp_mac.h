#pragma once
// MAC addresses come from the interface, not from efuses. WiFi.macAddress()
// reads the real one through SIOCGIFHWADDR; this exists for callers that ask
// ESP-IDF directly, and tells them it cannot help.
#include <cstdint>
#define ESP_MAC_WIFI_STA 0
#define ESP_MAC_WIFI_SOFTAP 1
#define ESP_MAC_BT 2

inline int esp_read_mac(uint8_t*, int) { return -1; }
inline int esp_efuse_mac_get_default(uint8_t*) { return -1; }
