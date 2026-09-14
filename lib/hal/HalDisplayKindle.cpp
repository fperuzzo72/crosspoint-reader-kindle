// HalDisplay for the Kindle.
//
// Replaces HalDisplay.cpp, which forwards everything to FreeInkDisplay. Here
// the panel half is KindleFrameBuffer (mmap of /dev/fb0 plus FBInk's refresh
// ioctls) and the framebuffer CrossPoint composes into is ours to own, because
// the panel half only ever receives a finished frame.
//
// Excluding this file's counterpart from the build is what removes the whole
// PanelDriver and EpdBus tree, which is where 251 of the undefined symbols in
// the first link attempt came from.
//
// ON GRAYSCALE: CrossPoint's dual-plane machinery exists because the ESP32
// panels need host-driven LUT tricks to get past two levels, and none of those
// tricks apply here. What does carry over is the plane ENCODING, which is a
// perfectly good way for a 1bpp renderer to say "this pixel is one of four
// grays". So the planes are taken at face value and composed into the 8bpp
// frame the EPDC wants. See the grayscale section below for the details, and
// note that the two-waveform sequence the ESP32 needs collapses to one here.

#include "HalDisplay.h"

#if FREEINK_DEVICE_KINDLE

#include <cstdlib>
#include <cstring>

namespace {

crosspoint::kindle::Waveform toWaveform(const HalDisplay::RefreshMode mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH:
      return crosspoint::kindle::Waveform::Full;
    case HalDisplay::HALF_REFRESH:
      return crosspoint::kindle::Waveform::Half;
    case HalDisplay::FAST_REFRESH:
    default:
      return crosspoint::kindle::Waveform::Fast;
  }
}

}  // namespace

HalDisplay::HalDisplay() = default;

HalDisplay::~HalDisplay() {
  std::free(frameBuffer);
  frameBuffer = nullptr;
}

HalDisplay::Controller HalDisplay::getController() const { return BoardConfig::ACTIVE.displayController; }

void HalDisplay::begin(const bool seamless) {
  (void)seamless;  // nothing to resync: the kernel owns the panel's state
  if (frameBuffer == nullptr) {
    frameBuffer = static_cast<uint8_t*>(std::malloc(BUFFER_SIZE));
  }
  if (frameBuffer != nullptr) {
    std::memset(frameBuffer, 0xFF, BUFFER_SIZE);
  }
  panel.begin();
}

// --- framebuffer composition -------------------------------------------------

void HalDisplay::clearScreen(const uint8_t color) const {
  if (frameBuffer != nullptr) {
    std::memset(frameBuffer, color, BUFFER_SIZE);
  }
}

void HalDisplay::drawImage(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                           const uint16_t h, const bool fromProgmem) const {
  (void)fromProgmem;  // PROGMEM is nothing here; see arduino-shim/Arduino.h
  if (imageData == nullptr || frameBuffer == nullptr) {
    return;
  }
  const uint16_t srcRowBytes = static_cast<uint16_t>((w + 7) / 8);
  for (uint16_t row = 0; row < h; ++row) {
    const uint16_t dstY = static_cast<uint16_t>(y + row);
    if (dstY >= DISPLAY_HEIGHT) {
      break;
    }
    for (uint16_t col = 0; col < w; ++col) {
      const uint16_t dstX = static_cast<uint16_t>(x + col);
      if (dstX >= DISPLAY_WIDTH) {
        break;
      }
      const uint8_t srcBit = imageData[row * srcRowBytes + (col >> 3)] & (0x80u >> (col & 7u));
      uint8_t& dst = frameBuffer[dstY * DISPLAY_WIDTH_BYTES + (dstX >> 3)];
      const uint8_t mask = static_cast<uint8_t>(0x80u >> (dstX & 7u));
      if (srcBit != 0) {
        dst = static_cast<uint8_t>(dst | mask);
      } else {
        dst = static_cast<uint8_t>(dst & ~mask);
      }
    }
  }
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                                      const uint16_t h, const bool fromProgmem) const {
  (void)fromProgmem;
  if (imageData == nullptr || frameBuffer == nullptr) {
    return;
  }
  // Transparent means a SET bit (white) leaves what is underneath alone; only
  // clear bits paint. That is the polarity the rest of the tree assumes.
  const uint16_t srcRowBytes = static_cast<uint16_t>((w + 7) / 8);
  for (uint16_t row = 0; row < h; ++row) {
    const uint16_t dstY = static_cast<uint16_t>(y + row);
    if (dstY >= DISPLAY_HEIGHT) {
      break;
    }
    for (uint16_t col = 0; col < w; ++col) {
      const uint16_t dstX = static_cast<uint16_t>(x + col);
      if (dstX >= DISPLAY_WIDTH) {
        break;
      }
      if ((imageData[row * srcRowBytes + (col >> 3)] & (0x80u >> (col & 7u))) != 0) {
        continue;  // white: transparent
      }
      frameBuffer[dstY * DISPLAY_WIDTH_BYTES + (dstX >> 3)] &= static_cast<uint8_t>(~(0x80u >> (dstX & 7u)));
    }
  }
}

uint8_t* HalDisplay::getFrameBuffer() const { return frameBuffer; }

// --- painting ----------------------------------------------------------------

void HalDisplay::displayBuffer(const RefreshMode mode, const bool turnOffScreen) {
  (void)turnOffScreen;  // the kernel powers the panel down on its own
  // A base staged for a grayscale pass that never came is superseded by this
  // paint; dropping the flag here keeps it from firing a stray refresh later.
  grayBaseStaged = false;
  if (frameBuffer != nullptr) {
    panel.display(frameBuffer, toWaveform(mode));
  }
}

void HalDisplay::displayBufferAsync(const RefreshMode mode) {
  grayBaseStaged = false;  // superseded, same as displayBuffer()
  if (frameBuffer != nullptr) {
    panel.displayStart(frameBuffer, toWaveform(mode));
  }
}

void HalDisplay::waitRefreshComplete() { panel.waitComplete(); }

// Measured on device: a full refresh submits in 20 ms and completes in 498.
// That is half a second of work the caller gets back, so the answer is yes.
bool HalDisplay::supportsAsyncRefresh() const { return true; }

void HalDisplay::refreshDisplay(const RefreshMode mode, const bool turnOffScreen) {
  displayBuffer(mode, turnOffScreen);
}

void HalDisplay::deepSleep() { panel.deepSleep(); }

bool HalDisplay::panelContentWasReplaced() const { return panel.panelContentWasReplaced(); }

bool HalDisplay::reinitAfterResume() {
  // A base staged for a grayscale pass cannot have survived the suspend: it
  // lived in panel memory, which is exactly what is in doubt.
  grayBaseStaged = false;
  if (!panel.reopen()) {
    return false;
  }
  // Repaint from our own framebuffer, which is ordinary heap and did survive.
  // FULL rather than FAST: DU is differential, and after a suspend there is no
  // trustworthy previous state to be differential against.
  if (frameBuffer != nullptr) {
    panel.display(frameBuffer, toWaveform(FULL_REFRESH));
  }
  return true;
}

// --- inversion ---------------------------------------------------------------

void HalDisplay::setInverted(const bool value) { inverted = value; }
bool HalDisplay::isInverted() const { return inverted; }

bool HalDisplay::toggleInverted() {
  inverted = !inverted;
  return inverted;
}

// --- framebuffer lending -----------------------------------------------------

uint8_t* HalDisplay::lendFrameBufferStorage(uint32_t* sizeOut) {
  // On the ESP32 this hands a memory-hungry phase the framebuffer's 48 KB
  // without freeing it, because that heap fragments badly. This device has
  // 256 MB and a Linux allocator, so the pressure that motivated it does not
  // exist. Honouring the contract anyway keeps the callers unchanged.
  if (lentStorage != nullptr || frameBuffer == nullptr) {
    return nullptr;
  }
  lentStorage = frameBuffer;
  if (sizeOut != nullptr) {
    *sizeOut = BUFFER_SIZE;
  }
  return lentStorage;
}

void HalDisplay::returnFrameBufferStorage() {
  if (lentStorage == nullptr) {
    return;
  }
  // The contract says the buffer comes back white and the caller redraws.
  std::memset(frameBuffer, 0xFF, BUFFER_SIZE);
  lentStorage = nullptr;
}

// --- geometry ----------------------------------------------------------------

uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }

// --- grayscale ---------------------------------------------------------------
//
// The renderer expresses grays as two 1bpp planes rather than as gray values,
// because the ESP32 panels need host-driven LUT tricks to get past two levels.
// None of that machinery applies here: the EPDC resolves 16 levels in hardware.
// What still applies is the ENCODING, and that is all this backend takes from
// it. The planes arrive, overlayGrayPlanesOnGray8() turns each claimed pixel
// into a gray value, and the EPDC does the rest.
//
// One thing this does not inherit is the ESP32's two-waveform sequence. There
// the base has to reach the panel before the gray masks can overlay it. Here
// panel memory is just bytes, so displayGrayscaleBase() stages the base WITHOUT
// running a waveform and displayGrayBuffer() paints the grays on top and runs
// one. A page of antialiased text costs a single refresh, not two.
//
// The waveform for that refresh is never FAST. FAST is DU, which is two-level:
// it would quantise every gray back to black or white and the whole pass would
// be wasted work. HALF (GL16) is the floor, and a caller asking for FULL still
// gets GC16.

HalDisplay::GrayscaleCapabilities HalDisplay::grayscaleCapabilities(const GrayscaleMode mode) const {
  GrayscaleCapabilities caps;
  if (mode != GrayscaleMode::Overlay) {
    // Absolute planes carry every pixel including the background, which this
    // backend could serve, but nothing routes to it without also claiming to
    // be a UC8279 (see EpubReaderActivity). Claiming support for a path that
    // cannot be reached would just be a lie in a capability struct.
    return caps;
  }
  caps.encoding = GrayscaleEncoding::OverlayMasks;
  // Combined: the base is deferred and joins the grays in one waveform.
  caps.base = GrayscaleBase::Combined;
  caps.stripUploads = false;   // no controller RAM to stream into
  caps.asyncBase = false;      // the base never gets its own waveform at all
  caps.stagingWhileBusy = true;  // staging is a memcpy into a mapping
  return caps;
}

bool HalDisplay::supportsAsyncGrayscaleBase() const { return false; }
bool HalDisplay::supportsStripGrayscale() const { return false; }
bool HalDisplay::combinesGrayscaleBase() const { return true; }

void HalDisplay::displayGrayscaleBase(const RefreshMode fallback, const bool turnOffScreen) {
  (void)turnOffScreen;
  if (frameBuffer == nullptr || !panel.stageFrame(frameBuffer)) {
    // Staging failed: fall back to painting it normally so the page is not
    // simply lost, and leave nothing deferred behind.
    grayBaseStaged = false;
    displayBuffer(fallback, turnOffScreen);
    return;
  }
  grayBaseMode = (fallback == FULL_REFRESH) ? FULL_REFRESH : HALF_REFRESH;
  grayBaseStaged = true;
}

bool HalDisplay::displayGrayscaleBase(const GrayscaleMode mode, const RefreshMode fallback,
                                      const bool turnOffScreen) {
  if (mode != GrayscaleMode::Overlay) {
    displayBuffer(fallback, turnOffScreen);
    return false;  // no grayscale pass follows
  }
  displayGrayscaleBase(fallback, turnOffScreen);
  return grayBaseStaged;
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  if (lsbBuffer == nullptr) return;
  if (grayLsbPlane == nullptr) grayLsbPlane = static_cast<uint8_t*>(std::malloc(BUFFER_SIZE));
  if (grayLsbPlane != nullptr) std::memcpy(grayLsbPlane, lsbBuffer, BUFFER_SIZE);
}

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  if (msbBuffer == nullptr) return;
  if (grayMsbPlane == nullptr) grayMsbPlane = static_cast<uint8_t*>(std::malloc(BUFFER_SIZE));
  if (grayMsbPlane != nullptr) std::memcpy(grayMsbPlane, msbBuffer, BUFFER_SIZE);
}

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  copyGrayscaleLsbBuffers(lsbBuffer);
  copyGrayscaleMsbBuffers(msbBuffer);
}

void HalDisplay::displayGrayBuffer(const bool turnOffScreen) {
  (void)turnOffScreen;
  const bool havePlanes = grayLsbPlane != nullptr && grayMsbPlane != nullptr;
  if (!havePlanes && !grayBaseStaged) {
    return;  // nothing was staged and nothing to overlay
  }
  if (havePlanes) {
    panel.stageGrayOverlay(grayLsbPlane, grayMsbPlane);
  }
  panel.refresh(toWaveform(grayBaseStaged ? grayBaseMode : HALF_REFRESH));
  grayBaseStaged = false;
}

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  // On the ESP32 this re-syncs the controller's differential baseline. There is
  // no such baseline here. What it must still do is rescue an abandoned pass:
  // if a base was staged and the grayscale render then bailed out (an OOM on
  // the way to the planes), the frame is sitting in panel memory with no
  // waveform ever run over it, and the reader would show the previous page.
  if (!grayBaseStaged) {
    return;
  }
  if (bwBuffer != nullptr) {
    panel.stageFrame(bwBuffer);
  }
  panel.refresh(toWaveform(grayBaseMode));
  grayBaseStaged = false;
}

void HalDisplay::writeGrayscalePlaneStrip(bool, const uint8_t*, uint16_t, uint16_t) {}

// X3-specific settle pass before its gray planes are written. The EPDC needs no
// equivalent: GL16 is itself the "text on white" waveform this preconditions for.
void HalDisplay::preconditionGrayscale() {}
void HalDisplay::preconditionGrayscale(uint16_t, uint16_t, uint16_t, uint16_t) {}

// The single instance the tree talks to. It lives here rather than in
// HalDisplay.cpp because that file compiles to nothing on this target, and
// guarding it out took this definition with it: the first link after the
// switch reported `display` undefined from every call site.
HalDisplay display;

#endif  // FREEINK_DEVICE_KINDLE
