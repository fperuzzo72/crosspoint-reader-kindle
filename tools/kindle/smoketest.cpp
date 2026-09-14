// First-light test for the Kindle display backend.
//
// This runs blind. Nobody is watching a console: it is launched by tapping a
// book on the Kindle's home screen, and whoever tapped it has walked away. So
// the point of this program is not really to paint pixels, it is to leave
// behind a log that answers every question the host could not answer over USB.
//
// In particular it dumps FBInk's view of the panel BEFORE asking
// KindleFrameBuffer::begin() to accept it. begin() refuses a geometry it does
// not recognise, which is the right behaviour in the reader but would be a
// dead end here: a refusal with no numbers tells us nothing about what the
// device actually is.

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "../../lib/hal/kindle/KindleFrameBuffer.h"

extern "C" {
#include "fbink.h"
}

namespace {

constexpr const char* LOG_PATH = "/mnt/us/crosspoint-smoketest.log";

FILE* logFile = nullptr;

void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logf(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  if (logFile != nullptr) {
    std::vfprintf(logFile, fmt, args);
    std::fputc('\n', logFile);
    std::fflush(logFile);
  }
  va_end(args);
}

void logBlank() {
  if (logFile != nullptr) {
    std::fputc('\n', logFile);
    std::fflush(logFile);
  }
}

int64_t monotonicMs() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

// A pattern whose orientation is unambiguous on the panel. If width and height
// are transposed somewhere, or rows step by the wrong stride, this comes out
// visibly wrong rather than plausibly wrong: the wedge is asymmetric in both
// axes and the corner block only touches one corner.
void drawPattern(uint8_t* fb, const uint16_t width, const uint16_t height) {
  const uint16_t rowBytes = static_cast<uint16_t>(width / 8);
  std::memset(fb, 0xFF, static_cast<size_t>(rowBytes) * height);  // white

  auto setBlack = [&](const uint16_t x, const uint16_t y) {
    fb[static_cast<size_t>(y) * rowBytes + (x >> 3)] &= static_cast<uint8_t>(~(0x80u >> (x & 7u)));
  };

  // Solid block in the top-left 100x100 only.
  for (uint16_t y = 0; y < 100 && y < height; ++y) {
    for (uint16_t x = 0; x < 100 && x < width; ++x) {
      setBlack(x, y);
    }
  }
  // Widening wedge down the left edge: row y is y/4 px wide.
  for (uint16_t y = 0; y < height; ++y) {
    const uint16_t w = static_cast<uint16_t>(y / 4);
    for (uint16_t x = 0; x < w && x < width; ++x) {
      setBlack(x, y);
    }
  }
  // One-pixel border, so a cropped or offset frame is obvious.
  for (uint16_t x = 0; x < width; ++x) {
    setBlack(x, 0);
    setBlack(x, static_cast<uint16_t>(height - 1));
  }
  for (uint16_t y = 0; y < height; ++y) {
    setBlack(0, y);
    setBlack(static_cast<uint16_t>(width - 1), y);
  }
}

}  // namespace

int main() {
  logFile = std::fopen(LOG_PATH, "w");

  const time_t now = time(nullptr);
  logf("CrossPoint Kindle display smoke test");
  logf("run at: %s", ctime(&now));
  logf("fbink version: %s", fbink_version());

  // --- what the device actually is, before anything can refuse it ----------
  const int fbfd = fbink_open();
  if (fbfd < 0) {
    logf("FATAL: fbink_open() failed. Is this running on a Kindle?");
    return 1;
  }

  FBInkConfig cfg{};
  cfg.is_quiet = true;
  if (fbink_init(fbfd, &cfg) < 0) {
    logf("FATAL: fbink_init() failed.");
    fbink_close(fbfd);
    return 1;
  }

  FBInkState state{};
  fbink_get_state(&cfg, &state);
  logBlank();
  logf("--- panel as reported by the kernel ---");
  logf("device codename : %s", state.device_codename);
  logf("device platform : %s", state.device_platform);
  logf("device id       : %u", static_cast<unsigned>(state.device_id));
  logf("screen          : %ux%u", state.screen_width, state.screen_height);
  logf("view            : %ux%u", state.view_width, state.view_height);
  logf("bpp             : %u", state.bpp);
  logf("scanline stride : %u", state.scanline_stride);
  logf("current rotation: %u", state.current_rota);
  logBlank();
  logf("this build expects %ux%u at 8bpp", crosspoint::kindle::KT3_WIDTH, crosspoint::kindle::KT3_HEIGHT);
  fbink_close(fbfd);

  // --- now the backend itself ----------------------------------------------
  crosspoint::kindle::KindleFrameBuffer fb;
  if (!fb.begin()) {
    logBlank();
    logf("RESULT: begin() refused this panel. Compare the numbers above with");
    logf("        KT3_WIDTH/KT3_HEIGHT in lib/hal/kindle/KindleFrameBuffer.h.");
    return 1;
  }
  logf("begin() accepted the panel: %ux%u", fb.width(), fb.height());

  static uint8_t frame[crosspoint::kindle::KT3_BUFFER_SIZE];
  drawPattern(frame, fb.width(), fb.height());

  struct Step {
    const char* name;
    crosspoint::kindle::Waveform waveform;
  };
  const Step steps[] = {
      {"Full (GC16, flashing)", crosspoint::kindle::Waveform::Full},
      {"Half (GL16)", crosspoint::kindle::Waveform::Half},
      {"Fast (DU)", crosspoint::kindle::Waveform::Fast},
  };

  // Prove the pixels landed BEFORE trusting any timing. The first run of this
  // test reported success while every blit silently failed, because nothing
  // ever looked at panel memory afterwards. Timings of a paint that did not
  // happen are worse than no timings: they look like progress.
  logBlank();
  logf("--- did the pixels actually land? ---");
  const bool painted = fb.display(frame, crosspoint::kindle::Waveform::Full);
  logf("display() returned: %s", painted ? "true" : "false");

  struct Probe {
    const char* what;
    uint16_t x;
    uint16_t y;
    uint8_t expect;
  };
  const Probe probes[] = {
      {"inside the corner block", 10, 10, crosspoint::kindle::GRAY_BLACK},
      {"background, clear of the wedge", 400, 400, crosspoint::kindle::GRAY_WHITE},
      {"right-hand border", static_cast<uint16_t>(fb.width() - 1), 400, crosspoint::kindle::GRAY_BLACK},
  };
  bool allGood = painted;
  for (const auto& p : probes) {
    const uint8_t got = fb.peekPixel(p.x, p.y);
    const bool ok = got == p.expect;
    allGood = allGood && ok;
    logf("  (%3u,%3u) %-32s got 0x%02X expected 0x%02X  %s", p.x, p.y, p.what, got, p.expect, ok ? "OK" : "WRONG");
  }

  if (!allGood) {
    logBlank();
    logf("RESULT: FAILED. The refresh path may still be fine, but the frame");
    logf("        never reached panel memory. Timings below are meaningless.");
  }

  logBlank();
  logf("--- waveform timings (blocking display(), full frame) ---");
  for (const auto& step : steps) {
    const int64_t start = monotonicMs();
    fb.display(frame, step.waveform);
    logf("%-24s %lld ms", step.name, static_cast<long long>(monotonicMs() - start));
  }

  // The async split is the interesting one: if these numbers are not much
  // smaller than the blocking ones above, the EPDC is not actually deferring
  // and displayStart() is lying about it.
  logBlank();
  logf("--- async split (displayStart returns, then waitComplete) ---");
  for (const auto& step : steps) {
    const int64_t start = monotonicMs();
    const bool deferred = fb.displayStart(frame, step.waveform);
    const int64_t submitted = monotonicMs();
    fb.waitComplete();
    logf("%-24s submit %lld ms, total %lld ms, deferred=%s", step.name, static_cast<long long>(submitted - start),
         static_cast<long long>(monotonicMs() - start), deferred ? "yes" : "no");
  }

  fb.deepSleep();
  fb.end();

  logBlank();
  if (allGood) {
    logf("RESULT: OK, and verified by reading panel memory back, not assumed.");
    logf("        The screen should show a white page with a 1px border, a");
    logf("        solid 100x100 block in ONE corner, and a wedge widening");
    logf("        downward along that same edge.");
  } else {
    logf("RESULT: FAILED, see the probe results above.");
  }
  return allGood ? 0 : 1;
}
