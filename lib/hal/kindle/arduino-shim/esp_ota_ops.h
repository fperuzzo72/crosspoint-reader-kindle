#pragma once
// esp_err_t and ESP_OK come from here, as they do in ESP-IDF.
#include "esp_err.h"
// OTA has no analogue here: this build is a file on a filesystem, not an image
// in a flash slot, and it updates by being copied over. Enough shape to link;
// every operation reports failure rather than pretending to have flashed.
#include <cstddef>

#include "esp_partition.h"

using esp_ota_handle_t = uint32_t;
inline int esp_ota_begin(const esp_partition_t*, size_t, esp_ota_handle_t*) { return -1; }
inline int esp_ota_write(esp_ota_handle_t, const void*, size_t) { return -1; }
inline int esp_ota_end(esp_ota_handle_t) { return -1; }
inline int esp_ota_set_boot_partition(const esp_partition_t*) { return -1; }
inline int esp_ota_abort(esp_ota_handle_t) { return -1; }
inline int esp_ota_mark_app_valid_cancel_rollback() { return -1; }
inline int esp_ota_mark_app_invalid_rollback_and_reboot() { return -1; }
