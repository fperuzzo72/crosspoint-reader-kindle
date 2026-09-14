#pragma once

// ESP-IDF's baked-in root certificate bundle.
//
// The ESP32 has no filesystem of trusted roots, so IDF compiles a bundle into
// the firmware and mbedtls attaches it through this call. A Linux system has
// the real thing: /etc/ssl/certs, maintained by whoever maintains the device.
//
// Attaching therefore succeeds and does nothing, because the underlying TLS
// stack on this target should use the system store rather than a bundle frozen
// at build time. That is a better answer than either failing or shipping our
// own stale copy of the world's certificate authorities.

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

inline esp_err_t esp_crt_bundle_attach(void*) { return ESP_OK; }
inline void esp_crt_bundle_detach(void*) {}
inline void esp_crt_bundle_set(const uint8_t*, size_t) {}
