// Timing and ESP for the Kindle (Linux) build. String lives in
// ArduinoString.cpp, which stays portable so the host tests can reach it.

#include "ArduinoCompat.h"

#include <sys/sysinfo.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace {

int64_t monotonicUs() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
}

// Captured on first use so millis() counts from process start, the way the
// Arduino core counts from boot.
int64_t epochUs() {
  static const int64_t start = monotonicUs();
  return start;
}

}  // namespace

uint32_t millis() { return static_cast<uint32_t>((monotonicUs() - epochUs()) / 1000); }

uint32_t micros() { return static_cast<uint32_t>(monotonicUs() - epochUs()); }

void delay(const uint32_t ms) {
  timespec ts{};
  ts.tv_sec = static_cast<time_t>(ms / 1000);
  ts.tv_nsec = static_cast<long>(ms % 1000) * 1000000L;
  // Resume across signals rather than returning early: callers treat delay()
  // as "at least this long".
  while (nanosleep(&ts, &ts) == -1) {
    // errno is EINTR; ts now holds the remainder.
  }
}

void delayMicroseconds(const uint32_t us) {
  timespec ts{};
  ts.tv_sec = static_cast<time_t>(us / 1000000);
  ts.tv_nsec = static_cast<long>(us % 1000000) * 1000L;
  while (nanosleep(&ts, &ts) == -1) {
  }
}

// ------------------------------------------------------------------- ESP ---

EspClass ESP;

uint32_t EspClass::getFreeHeap() const {
  struct sysinfo info {};
  if (sysinfo(&info) != 0) {
    return 0;
  }
  const uint64_t bytes = static_cast<uint64_t>(info.freeram) * info.mem_unit;
  return static_cast<uint32_t>(std::min<uint64_t>(bytes, UINT32_MAX));
}

uint32_t EspClass::getMaxAllocHeap() const { return getFreeHeap(); }

void EspClass::restart() const {
  // Re-exec this binary. /proc/self/exe survives the original argv[0] being
  // a relative path the cwd no longer resolves.
  execl("/proc/self/exe", "crosspoint", static_cast<char*>(nullptr));
  // Only reached if exec failed; the caller was promised this never returns.
  _exit(1);
}
