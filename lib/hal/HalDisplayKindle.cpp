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
// ON GRAYSCALE, and this is a real limitation worth stating rather than
// burying: CrossPoint's dual-plane grayscale machinery exists because the
// ESP32 panels need host-driven LUT tricks to get more than two levels. The
// Kindle's EPDC does 16 levels in hardware through GC16 and GL16, so none of
// that machinery applies and every plane method here is a no-op. The cost is
// that content renders 1-bit for now, since the buffer CrossPoint composes is
// 1bpp. Getting true grayscale means feeding the panel an 8bpp frame directly,
// which this backend is already positioned to do (the expansion is the only
// thing in the way) but which needs the renderer to produce more than one bit
// per pixel first. That is a later, worthwhile change, not a missing piece of
// this one.

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
  if (frameBuffer != nullptr) {
    panel.display(frameBuffer, toWaveform(mode));
  }
}

void HalDisplay::displayBufferAsync(const RefreshMode mode) {
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

// --- grayscale: all no-ops, see the note at the top --------------------------

HalDisplay::GrayscaleCapabilities HalDisplay::grayscaleCapabilities(GrayscaleMode mode) const {
  (void)mode;
  return {};  // no host-driven planes on this panel
}

bool HalDisplay::supportsAsyncGrayscaleBase() const { return false; }
bool HalDisplay::supportsStripGrayscale() const { return false; }
bool HalDisplay::combinesGrayscaleBase() const { return false; }

void HalDisplay::displayGrayscaleBase(const RefreshMode fallback, const bool turnOffScreen) {
  // With no plane machinery, the "base" is simply the frame.
  displayBuffer(fallback, turnOffScreen);
}

bool HalDisplay::displayGrayscaleBase(GrayscaleMode mode, const RefreshMode fallback, const bool turnOffScreen) {
  (void)mode;
  displayBuffer(fallback, turnOffScreen);
  return false;  // no grayscale pass follows
}

void HalDisplay::copyGrayscaleBuffers(const uint8_t*, const uint8_t*) {}
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t*) {}
void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t*) {}
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t*) {}
void HalDisplay::displayGrayBuffer(const bool) {}
void HalDisplay::writeGrayscalePlaneStrip(bool, const uint8_t*, uint16_t, uint16_t) {}
void HalDisplay::preconditionGrayscale() {}
void HalDisplay::preconditionGrayscale(uint16_t, uint16_t, uint16_t, uint16_t) {}

// The single instance the tree talks to. It lives here rather than in
// HalDisplay.cpp because that file compiles to nothing on this target, and
// guarding it out took this definition with it: the first link after the
// switch reported `display` undefined from every call site.
HalDisplay display;

#endif  // FREEINK_DEVICE_KINDLE
