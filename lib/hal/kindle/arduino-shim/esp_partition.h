#pragma once

// The flash partition table has no analogue here: this build is a file on a
// filesystem, not an image in a slot, and OTA is not how it updates. Enough of
// the shape exists to link.

#include <cstddef>
#include <cstdint>

using esp_partition_t = struct { uint32_t address; uint32_t size; const char* label; };

inline const esp_partition_t* esp_ota_get_running_partition() { return nullptr; }
inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return nullptr; }
