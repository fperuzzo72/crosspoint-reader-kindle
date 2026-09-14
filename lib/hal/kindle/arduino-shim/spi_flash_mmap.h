#pragma once
// Memory-mapped flash. This build is a file read through a filesystem, not an
// image mapped out of SPI flash, so there is nothing to map.
#include <cstddef>
#include <cstdint>
// The erase-block size callers align writes to. There is no flash here, but
// the constant appears in size arithmetic, so it keeps the ESP32 value.
#define SPI_FLASH_SEC_SIZE 4096

using spi_flash_mmap_handle_t = uint32_t;
inline int spi_flash_mmap(size_t, size_t, int, const void**, spi_flash_mmap_handle_t*) { return -1; }
inline void spi_flash_munmap(spi_flash_mmap_handle_t) {}
