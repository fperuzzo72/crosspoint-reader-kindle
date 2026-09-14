#pragma once
// eFuses are one-time-programmable bits in the ESP32's silicon: chip revision,
// MAC, security flags. Nothing analogous is readable here, and the identity
// callers actually want (the MAC) comes from the interface instead; see
// WiFi.macAddress().
#include <cstddef>
#include <cstdint>
// Field descriptors are arrays of pointers in IDF; a null-terminated empty one
// is a valid "no such field" that the read call below then refuses anyway.
struct esp_efuse_desc_t {
  uint32_t efuse_block;
  uint8_t bit_start;
  uint8_t bit_count;
};
inline const esp_efuse_desc_t* ESP_EFUSE_USER_DATA[] = {nullptr};

inline uint32_t esp_efuse_get_pkg_ver() { return 0; }
inline int esp_efuse_read_field_blob(const esp_efuse_desc_t**, void*, size_t) { return -1; }
inline int esp_efuse_write_field_blob(const esp_efuse_desc_t**, const void*, size_t) { return -1; }
