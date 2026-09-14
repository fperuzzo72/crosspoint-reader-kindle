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

bool KindleFrameBuffer::reopen() {
  const uint16_t previousStride = static_cast<uint16_t>(stride);
  end();
  if (!begin()) {
    std::fprintf(stderr, "[kindle] could not reopen /dev/fb0 after resume; the panel is no longer ours\n");
    return false;
  }
  std::fprintf(stderr, "[kindle] reopened /dev/fb0: %ux%u, stride %u%s\n", panelWidth, panelHeight, stride,
               previousStride != 0 && previousStride != stride ? " (CHANGED across the suspend)" : "");
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

bool KindleFrameBuffer::stageFrame(const uint8_t* frame) {
  if (!isOpen() || fbMem == nullptr || frame == nullptr) {
    return false;
  }
  // A waveform in flight is reading panel memory; overwriting it mid-refresh
  // is how you get a torn frame that no later paint fully repairs.
  if (hasPending) {
    waitComplete();
  }
  blit(frame);
  // Record even though no waveform follows. The sample answers "is what WE put
  // in panel memory still there", and staging is us putting something there:
  // without this, the window between staging a grayscale base and committing
  // its planes looks exactly like an outside writer, and the repaint it
  // provokes stages again. The log showed that loop as repainting (1) over and
  // over, each repaint sticking and then immediately appearing to be undone.
  rememberPanelContent();
  return true;
}

bool KindleFrameBuffer::stageGrayOverlay(const uint8_t* lsbPlane, const uint8_t* msbPlane) {
  if (!isOpen() || fbMem == nullptr || lsbPlane == nullptr || msbPlane == nullptr) {
    return false;
  }
  if (hasPending) {
    waitComplete();
  }
  overlayGrayPlanesOnGray8(lsbPlane, msbPlane, fbMem, panelWidth, panelHeight,
                           static_cast<uint16_t>(panelWidth / 8), stride);
  rememberPanelContent();
  return true;
}

bool KindleFrameBuffer::refresh(const Waveform waveform) {
  if (!isOpen()) {
    return false;
  }
  if (hasPending) {
    waitComplete();
  }
  FBInkConfig cfg = refreshConfig(waveform);
  if (fbink_refresh(fbfd, 0, 0, panelWidth, panelHeight, &cfg) < 0) {
    return false;
  }
  pendingMarker = fbink_get_last_marker();
  hasPending = true;
  waitComplete();
  rememberPanelContent();
  return true;
}

size_t KindleFrameBuffer::sampleOffsetAt(const int index) const {
  // Evenly spaced, then nudged by a prime so the points do not all land on the
  // same column of every row and miss a vertical band entirely.
  const size_t span = mapLen / PANEL_SAMPLES;
  const size_t base = static_cast<size_t>(index) * span;
  return (base + static_cast<size_t>(index) * 619u) % mapLen;
}

void KindleFrameBuffer::rememberPanelContent() {
  if (fbMem == nullptr || mapLen < PANEL_SAMPLES) {
    haveContentSample = false;
    return;
  }
  for (int i = 0; i < PANEL_SAMPLES; ++i) {
    contentSample[i] = fbMem[sampleOffsetAt(i)];
  }
  haveContentSample = true;
}

bool KindleFrameBuffer::panelContentWasReplaced() const {
  if (!haveContentSample || fbMem == nullptr || mapLen < PANEL_SAMPLES) {
    return false;
  }
  // The threshold is low on purpose, and the first version got this wrong by
  // reasoning about the wrong thing. "A blanking pass changes nearly
  // everything" is true of the PANEL and false of the SAMPLES: a page of text
  // is already almost entirely white, so clearing it to white only changes the
  // pixels that carried ink, which is well under a fifth of them. A quarter of
  // the samples was therefore a threshold that could not fire for exactly the
  // case this exists to catch, and did not.
  //
  // Memory does not change on its own, so a single differing byte is already
  // real evidence of a second writer. A handful is asked for only so that a
  // sample taken in the middle of our own composition cannot count as one.
  int differing = 0;
  for (int i = 0; i < PANEL_SAMPLES; ++i) {
    if (fbMem[sampleOffsetAt(i)] != contentSample[i]) {
      ++differing;
      if (differing >= 4) {
        return true;
      }
    }
  }
  return false;
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
  rememberPanelContent();
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
