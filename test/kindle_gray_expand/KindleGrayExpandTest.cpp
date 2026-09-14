// The Kindle EPDC wants 8bpp gray; CrossPoint composes 1bpp. Everything else
// in the Kindle display backend needs the device, so this expansion is the one
// part whose correctness is pinned down here instead of on the desk.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "KindleFrameBuffer.h"

namespace {

using crosspoint::kindle::expand1bppToGray8;
using crosspoint::kindle::GRAY_BLACK;
using crosspoint::kindle::GRAY_WHITE;

// dstStride defaults to w (rows back to back); pass it explicitly to model the
// real framebuffer, whose rows are wider than the panel.
std::vector<uint8_t> expand(const std::vector<uint8_t>& src, const uint16_t w, const uint16_t h,
                            const uint16_t rowBytes, const uint32_t dstStride = 0) {
  const uint32_t stride = dstStride != 0 ? dstStride : w;
  std::vector<uint8_t> dst(static_cast<size_t>(stride) * h, 0x5A);
  expand1bppToGray8(src.data(), dst.data(), w, h, rowBytes, stride);
  return dst;
}

TEST(KindleGrayExpand, SetBitIsWhiteClearBitIsBlack) {
  // clearScreen()'s default fill is 0xFF and it means white, so the polarity
  // has to survive the expansion in that direction and not the other.
  EXPECT_EQ(expand({0xFF}, 8, 1, 1), std::vector<uint8_t>(8, GRAY_WHITE));
  EXPECT_EQ(expand({0x00}, 8, 1, 1), std::vector<uint8_t>(8, GRAY_BLACK));
}

TEST(KindleGrayExpand, MsbIsTheLeftmostPixel) {
  // 0x80 is the leftmost pixel white, the other seven black. Getting this
  // backwards mirrors every glyph on the panel and still "works".
  const auto out = expand({0x80}, 8, 1, 1);
  EXPECT_EQ(out[0], GRAY_WHITE);
  for (size_t i = 1; i < 8; ++i) {
    EXPECT_EQ(out[i], GRAY_BLACK) << "pixel " << i;
  }

  const auto right = expand({0x01}, 8, 1, 1);
  EXPECT_EQ(right[7], GRAY_WHITE);
  EXPECT_EQ(right[0], GRAY_BLACK);
}

TEST(KindleGrayExpand, RowsAdvanceByRowBytesNotByWidth) {
  // Two rows of 8 px carried in a buffer whose rows are 2 bytes wide: the
  // second byte of each row is padding and must be skipped, not consumed as
  // pixels. This is the shape of the bug the EpdFont emitter once shipped.
  const std::vector<uint8_t> src = {0xFF, 0x00,   // row 0: 8 white px, then padding
                                    0x00, 0xFF};  // row 1: 8 black px, then padding
  const auto out = expand(src, 8, 2, 2);

  for (size_t i = 0; i < 8; ++i) {
    EXPECT_EQ(out[i], GRAY_WHITE) << "row 0 px " << i;
  }
  for (size_t i = 8; i < 16; ++i) {
    EXPECT_EQ(out[i], GRAY_BLACK) << "row 1 px " << (i - 8);
  }
}

TEST(KindleGrayExpand, WidthNotAMultipleOfEightIgnoresTrailingPadBits) {
  // 12 px of white needs 2 bytes; the low 4 bits of the second byte are pad.
  // They are set here to the opposite colour so leaking them would show.
  const std::vector<uint8_t> src = {0xFF, 0xF0};
  const auto out = expand(src, 12, 1, 2);

  ASSERT_EQ(out.size(), 12u);
  for (size_t i = 0; i < 12; ++i) {
    EXPECT_EQ(out[i], GRAY_WHITE) << "px " << i;
  }
}

TEST(KindleGrayExpand, DestinationStrideWiderThanThePanelDoesNotShear) {
  // The KT3 reports a 608-byte scanline stride for a 600px panel, measured on
  // device. Stepping the destination by width instead of by stride walks every
  // row 8px further left than the last, which shears the whole image.
  const std::vector<uint8_t> src = {0xFF,   // row 0: 8 white px
                                    0x00};  // row 1: 8 black px
  const uint32_t stride = 12;               // 8 px of panel, 4 bytes of padding
  const auto out = expand(src, 8, 2, 1, stride);

  ASSERT_EQ(out.size(), 24u);
  for (size_t i = 0; i < 8; ++i) {
    EXPECT_EQ(out[i], GRAY_WHITE) << "row 0 px " << i;
  }
  // Padding past the panel is not on screen: leave it alone rather than
  // clearing it, so this stays a pure row-wise write.
  for (size_t i = 8; i < 12; ++i) {
    EXPECT_EQ(out[i], 0x5A) << "row 0 padding byte " << i;
  }
  for (size_t i = 0; i < 8; ++i) {
    EXPECT_EQ(out[stride + i], GRAY_BLACK) << "row 1 px " << i;
  }
}

TEST(KindleGrayExpand, FullKt3FrameIsCoveredExactly) {
  using crosspoint::kindle::KT3_BUFFER_SIZE;
  using crosspoint::kindle::KT3_HEIGHT;
  using crosspoint::kindle::KT3_WIDTH;
  using crosspoint::kindle::KT3_WIDTH_BYTES;

  static_assert(KT3_WIDTH % 8 == 0, "KT3 width is byte-aligned");
  EXPECT_EQ(KT3_WIDTH_BYTES, 75u);
  EXPECT_EQ(KT3_BUFFER_SIZE, 60000u);

  // Every output pixel must be written: the scratch is pre-filled with a
  // sentinel, so an untouched pixel is visible as a hole rather than as
  // whatever the previous frame left in the buffer.
  const std::vector<uint8_t> src(KT3_BUFFER_SIZE, 0xFF);
  // 608 is the stride the device actually reports.
  constexpr uint32_t KT3_STRIDE = 608;
  const auto out = expand(src, KT3_WIDTH, KT3_HEIGHT, KT3_WIDTH_BYTES, KT3_STRIDE);

  ASSERT_EQ(out.size(), static_cast<size_t>(KT3_STRIDE) * KT3_HEIGHT);
  // Every on-panel pixel white, every padding byte untouched.
  EXPECT_EQ(std::count(out.begin(), out.end(), GRAY_WHITE), static_cast<long>(KT3_WIDTH) * KT3_HEIGHT);
  EXPECT_EQ(std::count(out.begin(), out.end(), 0x5A),
            static_cast<long>(KT3_STRIDE - KT3_WIDTH) * KT3_HEIGHT);
}

}  // namespace
