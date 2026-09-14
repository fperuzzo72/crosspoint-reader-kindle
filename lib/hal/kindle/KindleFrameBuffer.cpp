#include "KindleFrameBuffer.h"

#include <sys/mman.h>

#include <cstdio>
#include <cstdlib>

extern "C" {
#include "fbink.h"
}

namespace crosspoint::kindle {
namespace {

// FBInk is used ONLY for the refresh ioctls, which is where its real value
// is: the per-model quirk table. Pixels go into the mapped framebuffer
// directly. The first cut of this file blitted through fbink_print_raw_data
// and every call silently failed, because that entry point is compiled out of
// a MINIMAL build and nothing checked its return.

FBInkConfig refreshConfig(const Waveform waveform) {
  FBInkConfig cfg{};
  cfg.is_quiet = true;
  switch (waveform) {
    case Waveform::Full:
      cfg.wfm_mode = WFM_GC16;
      cfg.is_flashing = true;  // UPDATE_MODE_FULL: the black flash that scrubs ghosting
      break;
    case Waveform::Half:
      cfg.wfm_mode = WFM_GL16;
      cfg.is_flashing = false;
      break;
    case Waveform::Fast:
      cfg.wfm_mode = WFM_DU;
      cfg.is_flashing = false;
      break;
  }
  return cfg;
}

}  // namespace

KindleFrameBuffer::~KindleFrameBuffer() { end(); }

bool KindleFrameBuffer::begin() {
  if (isOpen()) {
    return true;
  }

  fbfd = fbink_open();
  if (fbfd < 0) {
    std::fprintf(stderr, "[kindle] fbink_open failed\n");
    return false;
  }

  FBInkConfig cfg{};
  cfg.is_quiet = true;
  if (fbink_init(fbfd, &cfg) < 0) {
    std::fprintf(stderr, "[kindle] fbink_init failed\n");
    end();
    return false;
  }

  FBInkState state{};
  fbink_get_state(&cfg, &state);

  // The resolution of a given Kindle model is fixed, so the buffer sizes are
  // compile-time. That is only safe if the kernel agrees: refuse rather than
  // paint past the end of the panel on a device this build was not made for.
  if (state.screen_width != KT3_WIDTH || state.screen_height != KT3_HEIGHT) {
    std::fprintf(stderr, "[kindle] panel is %ux%u, this build expects %ux%u (wrong device?)\n", state.screen_width,
                 state.screen_height, KT3_WIDTH, KT3_HEIGHT);
    end();
    return false;
  }
  if (state.bpp != 8) {
    std::fprintf(stderr, "[kindle] framebuffer is %ubpp, expected 8bpp grayscale\n", state.bpp);
    end();
    return false;
  }

  panelWidth = static_cast<uint16_t>(state.screen_width);
  panelHeight = static_cast<uint16_t>(state.screen_height);
  stride = state.scanline_stride;

  if (stride < panelWidth) {
    std::fprintf(stderr, "[kindle] stride %u is narrower than the %u px panel\n", stride, panelWidth);
    end();
    return false;
  }

  mapLen = static_cast<size_t>(stride) * panelHeight;
  void* mapped = mmap(nullptr, mapLen, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
  if (mapped == MAP_FAILED) {
    std::fprintf(stderr, "[kindle] mmap of %zu bytes of /dev/fb0 failed\n", mapLen);
    mapLen = 0;
    end();
    return false;
  }
  fbMem = static_cast<uint8_t*>(mapped);

  return true;
}

void KindleFrameBuffer::end() {
  if (hasPending) {
    waitComplete();
  }
  if (fbMem != nullptr) {
    munmap(fbMem, mapLen);
    fbMem = nullptr;
    mapLen = 0;
  }
  if (fbfd >= 0) {
    fbink_close(fbfd);
    fbfd = -1;
  }
  panelWidth = 0;
  panelHeight = 0;
  stride = 0;
}

void KindleFrameBuffer::blit(const uint8_t* frame) {
  // Straight into panel memory: no intermediate buffer, no copy, no library
  // call that can be compiled out from under us.
  expand1bppToGray8(frame, fbMem, panelWidth, panelHeight, static_cast<uint16_t>(panelWidth / 8), stride);
}

uint8_t KindleFrameBuffer::peekPixel(const uint16_t x, const uint16_t y) const {
  if (fbMem == nullptr || x >= panelWidth || y >= panelHeight) {
    return 0;
  }
  return fbMem[static_cast<size_t>(y) * stride + x];
}

bool KindleFrameBuffer::display(const uint8_t* frame, const Waveform waveform) {
  if (!displayStart(frame, waveform)) {
    return isOpen() && fbMem != nullptr;  // painted, just not deferred
  }
  waitComplete();
  return true;
}

bool KindleFrameBuffer::displayStart(const uint8_t* frame, const Waveform waveform) {
  if (!isOpen() || fbMem == nullptr || frame == nullptr) {
    return false;
  }
  if (hasPending) {
    // Two waveforms cannot be in flight at once on the EPDC.
    waitComplete();
  }

  blit(frame);

  FBInkConfig cfg = refreshConfig(waveform);
  if (fbink_refresh(fbfd, 0, 0, panelWidth, panelHeight, &cfg) < 0) {
    return false;
  }

  pendingMarker = fbink_get_last_marker();
  hasPending = true;
  return true;
}

void KindleFrameBuffer::waitComplete() {
  if (!hasPending) {
    return;
  }
  // A wait that fails is not fatal: the waveform still runs to completion on
  // the panel, the caller just loses the ability to know when. Dropping the
  // pending flag regardless keeps one bad wait from wedging every later paint.
  fbink_wait_for_complete(fbfd, pendingMarker);
  hasPending = false;
  pendingMarker = 0;
}

void KindleFrameBuffer::deepSleep() {
  // The EPDC has no host-driven sleep here the way an SPI controller does; the
  // kernel powers the panel down on its own once updates stop. Draining any
  // in-flight waveform first keeps a half-written frame off the screen.
  if (hasPending) {
    waitComplete();
  }
}

}  // namespace crosspoint::kindle
