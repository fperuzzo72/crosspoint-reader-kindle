// The 1bpp -> 8bpp expansion, deliberately kept in its own translation unit.
//
// Everything else in this backend needs a Kindle (or at least FBInk and a
// /dev/fb0) to do anything at all. This does not, so it is the piece the host
// test suite can actually hold to account. Splitting it out is what makes that
// test link without dragging libfbink into the host build.

#include "KindleFrameBuffer.h"

namespace crosspoint::kindle {

void expand1bppToGray8(const uint8_t* src, uint8_t* dst, const uint16_t width, const uint16_t height,
                       const uint16_t srcRowBytes, const uint32_t dstRowBytes) {
  for (uint16_t y = 0; y < height; ++y) {
    const uint8_t* row = src + static_cast<size_t>(y) * srcRowBytes;
    // Destination rows step by the framebuffer's stride, which is wider than
    // the panel (608 vs 600 on the KT3). Stepping by width instead shears the
    // image a little further left on every row.
    uint8_t* out = dst + static_cast<size_t>(y) * dstRowBytes;
    for (uint16_t x = 0; x < width; ++x) {
      // MSB is the leftmost pixel; a set bit is white.
      const uint8_t bit = static_cast<uint8_t>(0x80u >> (x & 7u));
      out[x] = (row[x >> 3] & bit) ? GRAY_WHITE : GRAY_BLACK;
    }
  }
}

void overlayGrayPlanesOnGray8(const uint8_t* lsbPlane, const uint8_t* msbPlane, uint8_t* dst, const uint16_t width,
                              const uint16_t height, const uint16_t srcRowBytes, const uint32_t dstRowBytes) {
  if (lsbPlane == nullptr || msbPlane == nullptr || dst == nullptr) {
    return;
  }
  for (uint16_t y = 0; y < height; ++y) {
    const uint8_t* lsbRow = lsbPlane + static_cast<size_t>(y) * srcRowBytes;
    const uint8_t* msbRow = msbPlane + static_cast<size_t>(y) * srcRowBytes;
    uint8_t* out = dst + static_cast<size_t>(y) * dstRowBytes;
    for (uint16_t x = 0; x < width; ++x) {
      const uint8_t bit = static_cast<uint8_t>(0x80u >> (x & 7u));
      const bool msb = (msbRow[x >> 3] & bit) != 0;
      // MSB clear means the pixel is not claimed by the overlay at all: the
      // encoding never emits (1,0), so there is nothing to read from the LSB
      // plane and the base keeps the pixel.
      if (!msb) {
        continue;
      }
      const bool lsb = (lsbRow[x >> 3] & bit) != 0;
      out[x] = lsb ? GRAY_DARK : GRAY_LIGHT;
    }
  }
}

}  // namespace crosspoint::kindle
