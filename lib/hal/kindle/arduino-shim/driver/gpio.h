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

// The ESP32 pin numbers appear as named constants in board code. They name
// nothing here; the values are kept so comparisons still behave.
#define GPIO_NUM_NC (-1)
#define GPIO_NUM_0 0
#define GPIO_NUM_1 1
#define GPIO_NUM_2 2
#define GPIO_NUM_3 3
#define GPIO_NUM_4 4
#define GPIO_NUM_5 5
#define GPIO_NUM_6 6
#define GPIO_NUM_7 7
#define GPIO_NUM_8 8
#define GPIO_NUM_9 9
#define GPIO_NUM_10 10
#define GPIO_NUM_11 11
#define GPIO_NUM_12 12
#define GPIO_NUM_13 13
#define GPIO_NUM_14 14
#define GPIO_NUM_15 15
#define GPIO_NUM_16 16
#define GPIO_NUM_20 20
#define GPIO_NUM_21 21

// esp_err_t and ESP_OK live in esp_err.h, as they do in ESP-IDF; defining them
// here too would be a redefinition wherever both are included.
#include "../esp_err.h"

#define GPIO_MODE_DISABLE 0
#define GPIO_MODE_INPUT 1
#define GPIO_MODE_OUTPUT 2
#define GPIO_MODE_OUTPUT_OD 3
#define GPIO_PULLUP_ONLY 0
#define GPIO_PULLDOWN_ONLY 1
#define GPIO_FLOATING 2

inline esp_err_t gpio_set_direction(gpio_num_t, int) { return ESP_OK; }
inline esp_err_t gpio_set_level(gpio_num_t, uint32_t) { return ESP_OK; }
inline int gpio_get_level(gpio_num_t) { return 0; }
inline esp_err_t gpio_set_pull_mode(gpio_num_t, int) { return ESP_OK; }

inline esp_err_t gpio_hold_en(gpio_num_t) { return ESP_OK; }
inline esp_err_t gpio_hold_dis(gpio_num_t) { return ESP_OK; }
inline esp_err_t gpio_deep_sleep_hold_en() { return ESP_OK; }
inline esp_err_t gpio_deep_sleep_hold_dis() { return ESP_OK; }
