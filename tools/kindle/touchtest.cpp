// Touch and display together: the first thing in this port where input
// produces output, which is the whole loop a reader needs.
//
// Draws a response to every gesture and logs it. Run it, touch the screen,
// then read /mnt/us/crosspoint-touch.log.
//
//   tap         small filled square where the finger was
//   long press  hollow square, twice the size
//   swipe       a line from where the finger landed to where it left
//
// Uses FAST (DU) for feedback so the screen keeps up with a finger, and only
// pays for a flashing GC16 at the start and the end.

#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "../../lib/hal/kindle/KindleFrameBuffer.h"
#include "../../lib/hal/kindle/KindleTouch.h"

namespace {

constexpr const char* LOG_PATH = "/mnt/us/crosspoint-touch.log";
constexpr int RUN_SECONDS = 45;

FILE* logFile = nullptr;
volatile sig_atomic_t stopRequested = 0;

void onSignal(int) { stopRequested = 1; }

void logBlank() {
  if (logFile != nullptr) {
    std::fputc('\n', logFile);
    std::fflush(logFile);
  }
}

void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logf(const char* fmt, ...) {
  va_list a;
  va_start(a, fmt);
  if (logFile != nullptr) {
    std::vfprintf(logFile, fmt, a);
    std::fputc('\n', logFile);
    std::fflush(logFile);
  }
  va_end(a);
}

using crosspoint::kindle::KT3_BUFFER_SIZE;

uint8_t frame[KT3_BUFFER_SIZE];
uint16_t fbWidth = 0;
uint16_t fbHeight = 0;

uint16_t rowBytes() { return static_cast<uint16_t>(fbWidth / 8); }

void clearWhite() { std::memset(frame, 0xFF, static_cast<size_t>(rowBytes()) * fbHeight); }

void setBlack(const int x, const int y) {
  if (x < 0 || y < 0 || x >= fbWidth || y >= fbHeight) {
    return;
  }
  frame[static_cast<size_t>(y) * rowBytes() + (x >> 3)] &= static_cast<uint8_t>(~(0x80u >> (x & 7u)));
}

void fillRect(const int cx, const int cy, const int half) {
  for (int y = cy - half; y <= cy + half; ++y) {
    for (int x = cx - half; x <= cx + half; ++x) {
      setBlack(x, y);
    }
  }
}

void strokeRect(const int cx, const int cy, const int half, const int thickness) {
  for (int y = cy - half; y <= cy + half; ++y) {
    for (int x = cx - half; x <= cx + half; ++x) {
      const bool onEdge = (y < cy - half + thickness) || (y > cy + half - thickness) ||
                          (x < cx - half + thickness) || (x > cx + half - thickness);
      if (onEdge) {
        setBlack(x, y);
      }
    }
  }
}

// Bresenham, so a swipe is drawn as the segment it was rather than as two dots.
void line(int x0, int y0, const int x1, const int y1, const int thickness) {
  const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
  const int dy = y1 > y0 ? y1 - y0 : y0 - y1;
  const int sx = x0 < x1 ? 1 : -1;
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  for (;;) {
    fillRect(x0, y0, thickness);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y0 += sy;
    }
  }
}

// A border plus corner ticks, so it is obvious the frame is whole and the
// right way up before a single finger lands.
void drawChrome() {
  for (int x = 0; x < fbWidth; ++x) {
    setBlack(x, 0);
    setBlack(x, fbHeight - 1);
  }
  for (int y = 0; y < fbHeight; ++y) {
    setBlack(0, y);
    setBlack(fbWidth - 1, y);
  }
  fillRect(20, 20, 12);  // solid block marks the TOP-LEFT corner only
}

int toX(const float nx) { return static_cast<int>(nx * static_cast<float>(fbWidth - 1) + 0.5F); }
int toY(const float ny) { return static_cast<int>(ny * static_cast<float>(fbHeight - 1) + 0.5F); }

}  // namespace

int main() {
  logFile = std::fopen(LOG_PATH, "w");
  const time_t now = time(nullptr);
  logf("CrossPoint Kindle touch test");
  logf("run at: %s", ctime(&now));

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  crosspoint::kindle::KindleFrameBuffer fb;
  if (!fb.begin()) {
    logf("FATAL: display begin() failed");
    return 1;
  }
  fbWidth = fb.width();
  fbHeight = fb.height();
  logf("display: %ux%u", fbWidth, fbHeight);

  crosspoint::kindle::KindleTouchDevice touch;
  if (!touch.begin(fbWidth, fbHeight)) {
    logf("FATAL: touch begin() failed. No device reported ABS_MT_POSITION_X/Y.");
    return 1;
  }
  logf("touch: open");
  logBlank();
  logf("Touch the screen for %d seconds. Try taps, a long press, and swipes.", RUN_SECONDS);
  logBlank();

  clearWhite();
  drawChrome();
  fb.display(frame, crosspoint::kindle::Waveform::Full);

  const time_t deadline = time(nullptr) + RUN_SECONDS;
  int taps = 0;
  int longPresses = 0;
  int swipes = 0;

  while (time(nullptr) < deadline && stopRequested == 0) {
    // 30 ms keeps the long-press timer honest without spinning: the panel is
    // silent under a held finger, so this poll IS the clock for it.
    const crosspoint::kindle::GestureResult g = touch.update(30);
    if (!g) {
      continue;
    }

    switch (g.kind) {
      case crosspoint::kindle::Gesture::Tap:
        ++taps;
        logf("TAP        (%.3f, %.3f) -> px (%d, %d)  held %u ms", g.nx, g.ny, toX(g.nx), toY(g.ny), g.heldMs);
        fillRect(toX(g.nx), toY(g.ny), 10);
        break;

      case crosspoint::kindle::Gesture::LongPress:
        ++longPresses;
        logf("LONGPRESS  (%.3f, %.3f) -> px (%d, %d)  held %u ms", g.nx, g.ny, toX(g.nx), toY(g.ny), g.heldMs);
        strokeRect(toX(g.nx), toY(g.ny), 22, 3);
        break;

      case crosspoint::kindle::Gesture::Swipe:
        ++swipes;
        logf("SWIPE      (%.3f, %.3f) -> (%.3f, %.3f)  px (%d,%d)->(%d,%d)  %u ms", g.nx, g.ny, g.nxEnd, g.nyEnd,
             toX(g.nx), toY(g.ny), toX(g.nxEnd), toY(g.nyEnd), g.heldMs);
        line(toX(g.nx), toY(g.ny), toX(g.nxEnd), toY(g.nyEnd), 2);
        break;

      default:
        continue;
    }

    fb.display(frame, crosspoint::kindle::Waveform::Fast);
  }

  logBlank();
  logf("--- %d taps, %d long presses, %d swipes ---", taps, longPresses, swipes);
  if (taps + longPresses + swipes == 0) {
    logf("NOTHING REGISTERED. The device opened but no gesture completed.");
  }

  clearWhite();
  drawChrome();
  fb.display(frame, crosspoint::kindle::Waveform::Full);
  touch.end();
  fb.end();
  return 0;
}
