#pragma once
// esp_err_t and ESP_OK come from here, as they do in ESP-IDF.
#include "esp_err.h"
// ESP-IDF's key/value store lives in a flash partition. Settings here live in
// files on a real filesystem, which is what HalStorage already uses, so there
// is nothing for this to back onto. Reports failure rather than losing writes
// silently.
#include <cstddef>
#include <cstdint>
using nvs_handle_t = uint32_t;
inline int nvs_flash_init() { return -1; }
inline int nvs_open(const char*, int, nvs_handle_t*) { return -1; }
inline int nvs_get_str(nvs_handle_t, const char*, char*, size_t*) { return -1; }
inline int nvs_set_str(nvs_handle_t, const char*, const char*) { return -1; }
inline int nvs_commit(nvs_handle_t) { return -1; }
inline void nvs_close(nvs_handle_t) {}
