#pragma once
// Chip identity and reset reasons. A Linux process has neither in any
// meaningful sense, so these report "unknown" rather than inventing a value.
#include <cstdint>

using esp_reset_reason_t = int;
#define ESP_RST_UNKNOWN 0
#define ESP_RST_POWERON 1
#define ESP_RST_SW 3

inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_UNKNOWN; }
