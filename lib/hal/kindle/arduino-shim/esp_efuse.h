#pragma once
// eFuses are one-time-programmable bits in the ESP32's silicon: chip revision,
// MAC, security flags. Nothing analogous is readable here, and the identity
// callers actually want (the MAC) comes from the interface instead; see
// WiFi.macAddress().
#include <cstdint>
inline uint32_t esp_efuse_get_pkg_ver() { return 0; }
inline int esp_efuse_read_field_blob(const void*, void*, size_t) { return -1; }
