#pragma once
// Address-range predicates for the ESP32's split memory map (internal SRAM vs
// PSRAM vs flash). A Linux process has one address space, so nothing here is
// "in PSRAM" and nothing is "in flash": every pointer is just a pointer.
#include <cstdint>
inline bool esp_ptr_internal(const void*) { return true; }
inline bool esp_ptr_external_ram(const void*) { return false; }
inline bool esp_ptr_in_drom(const void*) { return false; }
inline bool esp_ptr_dma_capable(const void*) { return true; }
