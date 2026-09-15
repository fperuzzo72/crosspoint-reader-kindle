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

// Ask the main loop to run CrossPoint's sleep. Raised from a menu selection and
// consumed once by the loop.
//
// A flag rather than a direct call, for the same reason the exit above is one:
// the sleep path pushes an activity and then runs ActivityManager::loop() to
// get it on the panel before the machine stops, and a menu selection is itself
// running inside that loop. Calling straight through would re-enter it.
void requestSleep();
bool consumeSleepRequest();

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

// Is the reader's storage actually mounted right now?
//
// It is not, for as long as the Kindle is presenting itself to a host as a USB
// mass storage device: the framework unmounts /mnt/us so the host can own the
// filesystem, and everything the reader draws a page from vanishes with it.
// The mount point stays behind as an empty directory, so this asks after a
// file that only exists when the real filesystem is there.
bool storageIsAttached();

// Battery charge, 0-100, or -1 when it cannot be read.
//
// There is no gauge on a bus for this process to talk to: the PMIC belongs to
// the kernel and the reading belongs to powerd, which publishes it. The
// device's own lipc property table was captured by the launcher's probe and
// lists "r Int battLevel" under com.lab126.powerd, so that is what is asked
// first rather than a sysfs path guessed from another model.
int batteryPercent();
#endif
}  // namespace HalSystem
