#pragma once
// ESP-IDF's error type and the handful of codes the tree checks against.
#include <cstdint>
using esp_err_t = int32_t;
#ifndef ESP_OK
#define ESP_OK 0
#endif
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_TIMEOUT 0x107
inline const char* esp_err_to_name(esp_err_t) { return "esp_err"; }
