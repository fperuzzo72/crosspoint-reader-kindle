#pragma once

// ESP-IDF GPIO stand-in for the Kindle build.
//
// BoardConfig.h includes this for exactly three names: gpio_num_t, and the
// gpio_hold_en/gpio_hold_dis pair it uses to latch a pin level across deep
// sleep (the charger-enable rail on the Xteink boards).
//
// None of that has a meaning here. The KT3 has no board pins CrossPoint
// drives, and its sleep is the kernel's business, not ours. So these exist to
// let BoardConfig compile, and they do nothing.
//
// They are NOT a promise that pin latching works. If a Kindle profile ever
// needs a real GPIO, this file should stop being a stub rather than quietly
// keep returning success.

#include <cstdint>

using gpio_num_t = int32_t;

using esp_err_t = int32_t;
#ifndef ESP_OK
#define ESP_OK 0
#endif

inline esp_err_t gpio_hold_en(gpio_num_t) { return ESP_OK; }
inline esp_err_t gpio_hold_dis(gpio_num_t) { return ESP_OK; }
inline esp_err_t gpio_deep_sleep_hold_en() { return ESP_OK; }
inline esp_err_t gpio_deep_sleep_hold_dis() { return ESP_OK; }
