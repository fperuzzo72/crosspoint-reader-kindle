#pragma once
// esp_err_t and ESP_OK come from here, as they do in ESP-IDF.
#include "esp_err.h"
// ESP-IDF's low-level Wi-Fi driver. There is no radio for this process to
// drive here; see WiFi.h for the reasoning. Every call reports failure.
#include <cstdint>

#include "WiFi.h"

using esp_err_t_wifi = int;
inline int esp_wifi_set_ps(int) { return -1; }
inline int esp_wifi_set_max_tx_power(int8_t) { return -1; }
inline int esp_wifi_get_max_tx_power(int8_t*) { return -1; }
inline int esp_wifi_start() { return -1; }
inline int esp_wifi_stop() { return -1; }
inline int esp_wifi_set_mode(wifi_mode_t) { return -1; }
