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

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
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
volatile sig_atomic_t sleepRequested = 0;
}  // namespace

void requestSleep() { sleepRequested = 1; }

bool consumeSleepRequest() {
  if (sleepRequested == 0) return false;
  sleepRequested = 0;
  return true;
}

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

bool storageIsAttached() {
  // The binary this process is running from. It cannot be absent while the
  // filesystem is mounted, and it cannot be present while it is not.
  return access("/mnt/us/crosspoint/crosspoint", F_OK) == 0;
}

namespace {

// Read an integer from the first line a command prints. Returns -1 on any
// failure, including a command that does not exist.
int readIntFromCommand(const char* command) {
  FILE* pipe = popen(command, "r");
  if (pipe == nullptr) return -1;
  char buf[64] = {};
  const char* line = std::fgets(buf, sizeof(buf), pipe);
  pclose(pipe);
  if (line == nullptr) return -1;
  char* end = nullptr;
  const long value = std::strtol(buf, &end, 10);
  if (end == buf) return -1;
  return static_cast<int>(value);
}

int readIntFromFile(const char* path) {
  FILE* f = std::fopen(path, "r");
  if (f == nullptr) return -1;
  char buf[64] = {};
  const char* line = std::fgets(buf, sizeof(buf), f);
  std::fclose(f);
  if (line == nullptr) return -1;
  char* end = nullptr;
  const long value = std::strtol(buf, &end, 10);
  if (end == buf) return -1;
  return static_cast<int>(value);
}

}  // namespace

int batteryPercent() {
  // Cached hard. The caller polls every 1.5s, and each miss here costs a fork
  // and an exec; a battery does not move fast enough for that to buy anything.
  static int cached = -1;
  static int64_t nextReadAtMs = 0;
  static bool saidSource = false;

  int64_t nowMs = 0;
  if (!readClockMs(CLOCK_MONOTONIC, nowMs)) return cached;
  if (cached >= 0 && nowMs < nextReadAtMs) return cached;
  nextReadAtMs = nowMs + 60000;

  // powerd first: the probe taken on the device lists battLevel, so this is
  // the one source known to exist here rather than inferred from a sibling
  // model. The sysfs paths after it are guesses kept as a fallback, and the
  // log names whichever answered so the guessing can stop.
  struct Source {
    const char* what;
    bool isCommand;
  };
  static const Source sources[] = {
      {"lipc-get-prop com.lab126.powerd battLevel 2>/dev/null", true},
      {"/sys/class/power_supply/bd71827_bat/capacity", false},
      {"/sys/class/power_supply/max77696-battery/capacity", false},
      {"/sys/devices/system/yoshi_battery/yoshi_battery0/battery_capacity", false},
  };

  for (const auto& source : sources) {
    const int value = source.isCommand ? readIntFromCommand(source.what) : readIntFromFile(source.what);
    if (value >= 0 && value <= 100) {
      if (!saidSource) {
        saidSource = true;
        std::fprintf(stderr, "[kindle] battery read from %s: %d%%\n", source.what, value);
      }
      cached = value;
      return cached;
    }
  }

  if (!saidSource) {
    saidSource = true;
    std::fprintf(stderr, "[kindle] no battery source answered; the gauge will read 0\n");
  }
  return cached;
}

bool resumedFromSuspend(uint32_t* const millisAsleep) {
  if (!clocksUsable) {
    return false;
  }

  int64_t monotonicMs = 0;
  int64_t boottimeMs = 0;
  if (!readClockMs(CLOCK_MONOTONIC, monotonicMs) || !readClockMs(CLOCK_BOOTTIME, boottimeMs)) {
    // A kernel without CLOCK_BOOTTIME cannot answer this question, and asking
    // it again every loop iteration would be a syscall per frame for nothing.
    //
    // Say so, loudly and once. The first version of this went quiet here, and a
    // detector that disables itself in silence is indistinguishable from one
    // that is working and finding nothing. That cost a round of testing.
    std::fprintf(stderr, "[kindle] CLOCK_BOOTTIME unavailable; suspend detection is off for this run\n");
    clocksUsable = false;
    return false;
  }

  if (!clocksInitialised) {
    // The first call establishes the baseline. Comparing against zero here
    // would report the whole uptime as a suspend.
    lastMonotonicMs = monotonicMs;
    lastBoottimeMs = boottimeMs;
    clocksInitialised = true;
    // Printed so a log can prove the detector is armed. "No suspend was
    // reported" and "nothing was ever watching" look identical otherwise.
    std::fprintf(stderr, "[kindle] suspend detection armed (monotonic %lds, boottime %lds)\n",
                 static_cast<long>(monotonicMs / 1000), static_cast<long>(boottimeMs / 1000));
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
