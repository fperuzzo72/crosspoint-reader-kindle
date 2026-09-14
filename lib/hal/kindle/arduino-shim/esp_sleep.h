#pragma once
// esp_err_t and ESP_OK come from here, as they do in ESP-IDF.
#include "esp_err.h"

// Deep sleep is the kernel's business on this target, not ours: a Kindle
// suspends itself and a reader process has no say in it. These exist so the
// power paths link, and they do nothing.

#include <cstdint>

using esp_sleep_wakeup_cause_t = int;
#define ESP_SLEEP_WAKEUP_UNDEFINED 0
#define ESP_SLEEP_WAKEUP_EXT0 2
#define ESP_SLEEP_WAKEUP_TIMER 4

inline void esp_deep_sleep_start() {}
inline int esp_sleep_enable_timer_wakeup(uint64_t) { return 0; }
inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return ESP_SLEEP_WAKEUP_UNDEFINED; }
inline int esp_sleep_config_gpio_isolate() { return 0; }
