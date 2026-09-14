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

}  // namespace crosspoint::kindle
