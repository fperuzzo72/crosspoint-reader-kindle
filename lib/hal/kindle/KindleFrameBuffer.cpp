#include "KindleFrameBuffer.h"

#include <cstdio>
#include <cstdlib>

extern "C" {
#include "fbink.h"
}

namespace crosspoint::kindle {
namespace {

// FBInk keeps its own notion of config per call. Two are needed: one that
// draws without refreshing, one that refreshes without drawing.
FBInkConfig blitConfig() {
  FBInkConfig cfg{};
  cfg.is_quiet = true;
  cfg.ignore_alpha = true;  // input is plain Y8, no alpha channel
  cfg.no_refresh = true;    // the refresh is issued separately, with a chosen waveform
  return cfg;
}

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

  gray = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(panelWidth) * panelHeight));
  if (gray == nullptr) {
    std::fprintf(stderr, "[kindle] out of memory for the %ux%u gray scratch\n", panelWidth, panelHeight);
    end();
    return false;
  }

  return true;
}

void KindleFrameBuffer::end() {
  if (hasPending) {
    waitComplete();
  }
  std::free(gray);
  gray = nullptr;
  if (fbfd >= 0) {
    fbink_close(fbfd);
    fbfd = -1;
  }
  panelWidth = 0;
  panelHeight = 0;
}

void KindleFrameBuffer::blit(const uint8_t* frame) {
  expand1bppToGray8(frame, gray, panelWidth, panelHeight, static_cast<uint16_t>(panelWidth / 8));

  FBInkConfig cfg = blitConfig();
  const size_t len = static_cast<size_t>(panelWidth) * panelHeight;
  fbink_print_raw_data(fbfd, gray, panelWidth, panelHeight, len, 0, 0, &cfg);
}

void KindleFrameBuffer::display(const uint8_t* frame, const Waveform waveform) {
  if (displayStart(frame, waveform)) {
    waitComplete();
  }
}

bool KindleFrameBuffer::displayStart(const uint8_t* frame, const Waveform waveform) {
  if (!isOpen() || frame == nullptr) {
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
