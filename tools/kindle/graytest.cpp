// Host test for the two pure pixel functions in the Kindle display backend.
//
// Neither needs a Kindle, and both are arithmetic whose failure mode is a page
// that looks subtly wrong in a way no photograph settles. The plane bits are
// not written by hand here: they are produced by grayPlanePixel(), the same
// function the renderer uses, driven the way the renderer drives it. That is
// the point of the test. Hand-written bits would only prove the backend agrees
// with my reading of the encoding, which is exactly what was in doubt.
//
//   c++ -std=c++17 -I lib/GfxRenderer -I lib/hal/kindle \
//       tools/kindle/graytest.cpp lib/hal/kindle/KindleGrayExpand.cpp -o /tmp/graytest

#include <BitmapHelpers.h>
#include <KindleFrameBuffer.h>

#include <cstdio>
#include <cstring>
#include <vector>

using namespace crosspoint::kindle;

namespace {

int failures = 0;

void check(const bool ok, const char* what) {
  if (!ok) {
    std::printf("  FAIL  %s\n", what);
    ++failures;
  }
}

constexpr uint16_t W = 600;
constexpr uint16_t H = 8;
constexpr uint16_t SRC_ROW = W / 8;   // 75
constexpr uint32_t DST_ROW = 608;     // the KT3's real stride, wider than the panel

void setBit(std::vector<uint8_t>& plane, const int x, const int y) {
  plane[static_cast<size_t>(y) * SRC_ROW + (x >> 3)] |= static_cast<uint8_t>(0x80u >> (x & 7u));
}

// Reproduce one plane pass the way EpubReaderActivity does it: clear the buffer
// to 0x00 and let the renderer's drawPixel() set a bit wherever grayPlanePixel
// says to write. In overlay mode `black` is always false, and drawPixel(state
// false) sets the bit.
void renderPlane(std::vector<uint8_t>& plane, const bool msb, const uint8_t* levels) {
  std::memset(plane.data(), 0x00, plane.size());
  for (uint16_t y = 0; y < H; ++y) {
    for (uint16_t x = 0; x < W; ++x) {
      const auto px = grayPlanePixel(levels[x], msb, /*absolute=*/false);
      if (px.write && !px.black) setBit(plane, x, y);
    }
  }
}

void testExpand() {
  std::printf("expand1bppToGray8\n");
  std::vector<uint8_t> src(static_cast<size_t>(SRC_ROW) * H, 0x00);
  std::vector<uint8_t> dst(static_cast<size_t>(DST_ROW) * H, 0x7E);  // sentinel

  // Leftmost pixel of each row white, everything else black: catches both the
  // MSB-first bit order and a stride mistake, which look identical otherwise.
  for (uint16_t y = 0; y < H; ++y) src[static_cast<size_t>(y) * SRC_ROW] = 0x80;

  expand1bppToGray8(src.data(), dst.data(), W, H, SRC_ROW, DST_ROW);

  for (uint16_t y = 0; y < H; ++y) {
    const uint8_t* row = dst.data() + static_cast<size_t>(y) * DST_ROW;
    check(row[0] == GRAY_WHITE, "MSB is the leftmost pixel");
    check(row[1] == GRAY_BLACK, "second pixel stays black");
    check(row[W - 1] == GRAY_BLACK, "last on-panel pixel");
    check(row[W] == 0x7E, "stride padding is left untouched");
    check(row[DST_ROW - 1] == 0x7E, "end-of-row padding is left untouched");
  }
  std::printf("  %d bytes per row in, %u out\n", SRC_ROW, DST_ROW);
}

void testOverlay() {
  std::printf("overlayGrayPlanesOnGray8\n");

  // Column x carries level x%4, so every level is exercised at every bit
  // position within a byte.
  std::vector<uint8_t> levels(W);
  for (uint16_t x = 0; x < W; ++x) levels[x] = static_cast<uint8_t>(x % 4);

  std::vector<uint8_t> lsb(static_cast<size_t>(SRC_ROW) * H);
  std::vector<uint8_t> msb(static_cast<size_t>(SRC_ROW) * H);
  renderPlane(lsb, /*msb=*/false, levels.data());
  renderPlane(msb, /*msb=*/true, levels.data());

  // The base the overlay lands on: alternating, so "kept the base" is
  // distinguishable from "wrote white" and from "wrote black".
  std::vector<uint8_t> dst(static_cast<size_t>(DST_ROW) * H);
  for (uint16_t y = 0; y < H; ++y) {
    uint8_t* row = dst.data() + static_cast<size_t>(y) * DST_ROW;
    for (uint32_t x = 0; x < DST_ROW; ++x) row[x] = (x & 1) ? GRAY_WHITE : GRAY_BLACK;
  }

  overlayGrayPlanesOnGray8(lsb.data(), msb.data(), dst.data(), W, H, SRC_ROW, DST_ROW);

  for (uint16_t y = 0; y < H; ++y) {
    const uint8_t* row = dst.data() + static_cast<size_t>(y) * DST_ROW;
    for (uint16_t x = 0; x < W; ++x) {
      const uint8_t base = (x & 1) ? GRAY_WHITE : GRAY_BLACK;
      switch (levels[x]) {
        case 0:  // black: not gray, the B/W base already painted it
        case 3:  // white: likewise
          check(row[x] == base, "unclaimed pixel keeps the base");
          break;
        case 1:
          check(row[x] == GRAY_DARK, "level 1 is dark");
          break;
        case 2:
          check(row[x] == GRAY_LIGHT, "level 2 is light");
          break;
        default:
          break;
      }
    }
    check(row[W] == GRAY_WHITE || row[W] == GRAY_BLACK, "padding untouched by the overlay");
  }

  // A pixel the planes never claim must not be touched even when the base is a
  // value the overlay itself could have written.
  std::vector<uint8_t> clear(static_cast<size_t>(SRC_ROW) * H, 0x00);
  std::vector<uint8_t> untouched(static_cast<size_t>(DST_ROW) * H, GRAY_DARK);
  overlayGrayPlanesOnGray8(clear.data(), clear.data(), untouched.data(), W, H, SRC_ROW, DST_ROW);
  bool allSame = true;
  for (const auto b : untouched) allSame = allSame && b == GRAY_DARK;
  check(allSame, "empty planes change nothing");

  std::printf("  levels 0..3 across %d columns x %d rows\n", W, H);
}

}  // namespace

int main() {
  testExpand();
  testOverlay();
  if (failures == 0) {
    std::printf("\nOK\n");
    return 0;
  }
  std::printf("\n%d FAILURES\n", failures);
  return 1;
}
