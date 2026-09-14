// HalSystem for the Kindle.
//
// The whole of this interface is about surviving a crash: the ESP32 version
// captures a stack walk into RTC_NOINIT memory, which survives a reset, then
// reads it back on the next boot to tell the user what happened.
//
// None of that applies here, and not because it is hard. A Linux process that
// crashes does not take the device with it: the kernel keeps running, writes a
// core file if enabled, and the launching scriptlet already redirects stderr
// into a log on the card. The evidence is better than what RTC_NOINIT could
// hold, and it is collected by the system rather than by us.
//
// So these are honest no-ops rather than a reimplementation of a mechanism the
// platform makes unnecessary. isRebootFromPanic() answers false because this
// process cannot reboot the device and would not know if something else had.

#include "HalSystem.h"

#if FREEINK_DEVICE_KINDLE

#include <BoardConfig.h>

#include <csignal>
#include <ctime>

namespace HalSystem {

void begin() {}

void checkPanic() {}

void clearPanic() {}

std::string getPanicInfo(const bool) {
  // Deliberately not an empty string dressed up as a report: callers show this
  // to the user, and "no panic recorded" is the truth on this target.
  return {};
}

bool isRebootFromPanic() { return false; }

namespace {
// Read by the main loop on every iteration, written by a UI activity. Both run
// on the same thread today; volatile sig_atomic_t costs nothing and keeps this
// correct if the signal handler ever needs to set it too.
volatile sig_atomic_t exitRequested = 0;
}  // namespace

void requestApplicationExit() { exitRequested = 1; }

bool applicationExitRequested() { return exitRequested != 0; }

namespace {

// Kernel 2.6.39 added it and this device runs 3.10, but the toolchain targets
// a glibc old enough that its headers predate the constant. The clock id is
// passed through to the kernel untouched, so defining it here is enough.
#ifndef CLOCK_BOOTTIME
#define CLOCK_BOOTTIME 7
#endif

bool readClockMs(const clockid_t id, int64_t& outMs) {
  timespec ts{};
  if (clock_gettime(id, &ts) != 0) {
    return false;
  }
  outMs = static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
  return true;
}

// Below this, the difference is scheduling noise rather than a suspend. The
// two clocks track each other to well under a millisecond while the machine is
// awake, so there is a lot of room between "noise" and "the user pressed
// power", and no reason to sit close to the edge.
constexpr int64_t SUSPEND_THRESHOLD_MS = 1000;

int64_t lastMonotonicMs = 0;
int64_t lastBoottimeMs = 0;
bool clocksInitialised = false;
bool clocksUsable = true;

}  // namespace

bool resumedFromSuspend(uint32_t* const millisAsleep) {
  if (!clocksUsable) {
    return false;
  }

  int64_t monotonicMs = 0;
  int64_t boottimeMs = 0;
  if (!readClockMs(CLOCK_MONOTONIC, monotonicMs) || !readClockMs(CLOCK_BOOTTIME, boottimeMs)) {
    // A kernel without CLOCK_BOOTTIME cannot answer this question, and asking
    // it again every loop iteration would be a syscall per frame for nothing.
    clocksUsable = false;
    return false;
  }

  if (!clocksInitialised) {
    // The first call establishes the baseline. Comparing against zero here
    // would report the whole uptime as a suspend.
    lastMonotonicMs = monotonicMs;
    lastBoottimeMs = boottimeMs;
    clocksInitialised = true;
    return false;
  }

  const int64_t asleepMs = (boottimeMs - lastBoottimeMs) - (monotonicMs - lastMonotonicMs);
  lastMonotonicMs = monotonicMs;
  lastBoottimeMs = boottimeMs;

  if (asleepMs < SUSPEND_THRESHOLD_MS) {
    return false;
  }
  if (millisAsleep != nullptr) {
    *millisAsleep = static_cast<uint32_t>(asleepMs);
  }
  return true;
}

}  // namespace HalSystem

#endif  // FREEINK_DEVICE_KINDLE
