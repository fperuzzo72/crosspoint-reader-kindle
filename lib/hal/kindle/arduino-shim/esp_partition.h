#pragma once
// esp_err_t and ESP_OK come from here, as they do in ESP-IDF.
#include "esp_err.h"

// The flash partition table has no analogue here: this build is a file on a
// filesystem, not an image in a slot, and OTA is not how it updates. Enough of
// the shape exists to link.

#include <cstddef>
#include <cstdint>

// A named struct, not a typedef of an anonymous one: callers declare
// functions taking const esp_partition_t*, and an anonymous type cannot match
// across translation units.
struct esp_partition_t {
  uint32_t address;
  uint32_t size;
  uint32_t type;
  uint32_t subtype;
  const char* label;
};

#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_ANY 0xFF
#define ESP_PARTITION_SUBTYPE_DATA_OTA 0x00
#define ESP_PARTITION_SUBTYPE_APP_OTA_0 0x10
#define ESP_PARTITION_SUBTYPE_APP_OTA_1 0x11

inline int esp_partition_read(const esp_partition_t*, size_t, void*, size_t) { return -1; }
inline int esp_partition_write(const esp_partition_t*, size_t, const void*, size_t) { return -1; }
inline int esp_partition_erase_range(const esp_partition_t*, size_t, size_t) { return -1; }
inline const esp_partition_t* esp_partition_find_first(int, int, const char*) { return nullptr; }

inline const esp_partition_t* esp_ota_get_running_partition() { return nullptr; }
inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return nullptr; }
