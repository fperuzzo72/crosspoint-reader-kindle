#pragma once

// ESP-ROM stand-in for the Kindle build.
//
// The SDK reaches for this for its ROM-resident printf and busy-wait, both of
// which exist on the ESP32 because the ordinary ones are not always safe to
// call (early boot, from an ISR). Neither constraint applies to a Linux
// process, so these map onto the ordinary ones.

#include <cstdio>
#include <cstdint>
#include <ctime>

#define esp_rom_printf printf

inline void esp_rom_delay_us(const uint32_t us) {
  timespec ts{};
  ts.tv_sec = static_cast<time_t>(us / 1000000);
  ts.tv_nsec = static_cast<long>(us % 1000000) * 1000L;
  while (nanosleep(&ts, &ts) == -1) {
  }
}

inline void ets_delay_us(const uint32_t us) { esp_rom_delay_us(us); }
