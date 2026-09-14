#pragma once

#include <BoardConfig.h>

#include <cstdint>
#include <string>

namespace HalSystem {
struct StackFrame {
  uint32_t sp;
  uint32_t spp[8];
};

void begin();

// Dump panic info to SD card if necessary
void checkPanic();
void clearPanic();

std::string getPanicInfo(bool full = false);
bool isRebootFromPanic();

#if FREEINK_DEVICE_KINDLE
// Ask the process to leave its main loop and exit.
//
// This exists on the Kindle and nowhere else, and the asymmetry is the point:
// on a reader whose firmware IS CrossPoint there is nothing to exit to, while
// here the app is a process that took over the panel of a device with its own
// UI underneath. Without a way out the only way back to the Kindle is a reset.
//
// The request is honoured by the main loop, not acted on here, so the activity
// that raised it finishes its frame first and nothing is left half-drawn.
void requestApplicationExit();
bool applicationExitRequested();

// True once for each time the device has come back from a suspend, with the
// time it spent there written to `millisAsleep` when non-null.
//
// Pressing the power button suspends the whole machine. The process is frozen
// mid-loop and thawed later with no idea anything happened, so it never
// repaints, and the panel comes back holding whatever the suspend left on it.
//
// Detecting it needs no lipc listener and no sysfs watch, because the kernel
// already keeps two clocks that disagree in exactly this situation:
// CLOCK_MONOTONIC stops while suspended and CLOCK_BOOTTIME keeps counting. The
// gap between how much each advanced IS the time spent asleep.
bool resumedFromSuspend(uint32_t* millisAsleep = nullptr);
#endif
}  // namespace HalSystem
