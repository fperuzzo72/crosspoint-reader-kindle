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

}  // namespace HalSystem

#endif  // FREEINK_DEVICE_KINDLE
