#pragma once
// Cycle counters and core identity from ESP-IDF's private headers. A Linux
// process has neither in any portable form, and nothing here needs them for
// correctness: they feed diagnostics.
#include <cstdint>
inline uint32_t esp_cpu_get_cycle_count() { return 0; }
inline int esp_cpu_get_core_id() { return 0; }
