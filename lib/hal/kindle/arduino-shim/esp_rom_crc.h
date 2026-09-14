#pragma once
// CRC32 from the ESP32's ROM.
//
// Implemented rather than stubbed: callers use it to validate stored data, and
// a stub returning a constant would make every integrity check pass, which is
// worse than not checking at all.
#include <cstdint>

uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t* buf, uint32_t len);
