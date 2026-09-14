// HalGPIO for the Kindle.
//
// Two thirds of this interface describes hardware the KT3 does not have, and
// saying so plainly is more useful than half-implementing it:
//
//  - No buttons CrossPoint can read. The device has exactly one physical
//    control, the power button, and the kernel consumes it for sleep and
//    shutdown before any userspace process sees it. So every button query
//    answers "not pressed", and the UI must be navigable by touch alone. It is:
//    FREEINK_CAP_TOUCH is set for this device and the touch-only devices
//    already in the tree (X4 Pro, Paper Mono, M5PaperS3) prove the paths work.
//
//  - No wake reasons and no sleep participation. On the ESP32 the firmware
//    decides to sleep and inspects why it woke. Here the kernel suspends and
//    resumes the whole device on its own schedule, and a reader process is
//    simply descheduled and rescheduled. Reporting Other is the truth.
//
//  - No home key. That is an X4 Pro capacitive control.
//
// What IS real is touch, through KindleTouchDevice: taps, long presses and
// swipes, in normalised coordinates, measured against the panel's own evdev
// stream.

#include "HalGPIO.h"

#include "HalDisplay.h"  // panel geometry, for the touch classifier's normalisation

#if FREEINK_DEVICE_KINDLE

#include <cstdio>

namespace {

// The panel is silent under a held finger, so the classifier's timer is the
// only thing that can notice a long press. Polling with a small timeout inside
// update() is what drives it; zero would spin, and a long wait would stall the
// UI loop.
constexpr int TOUCH_POLL_MS = 5;

}  // namespace

bool HalGPIO::isXteinkDevice() const { return false; }

bool HalGPIO::hasEdgeSideButtons() const { return false; }

void HalGPIO::begin() {
  touchOpen = touchDevice.begin(HalDisplay::DISPLAY_WIDTH, HalDisplay::DISPLAY_HEIGHT);
  if (!touchOpen) {
    // Worth a line: with no touch and no buttons, the reader is unreachable,
    // and silence here would look like a frozen UI rather than a missing input
    // device.
    std::fprintf(stderr, "[kindle] touch device did not open; the UI will not be reachable\n");
  }
}

void HalGPIO::update() {
  if (!touchOpen) {
    return;
  }
  // One gesture per frame. If two complete inside a single frame the second is
  // kept, because it is the more recent thing the user did; queueing them would
  // let taps arrive after the screen they targeted had already changed.
  const crosspoint::kindle::GestureResult g = touchDevice.update(TOUCH_POLL_MS);
  frameGesture = g ? g : crosspoint::kindle::GestureResult{};

  // USB edge detection lives here because wasUsbStateChanged() is const: the
  // sampling has to happen on the one call that is allowed to mutate.
  const bool usbNow = isUsbConnected();
  usbStateChanged = usbNow != lastUsbConnected;
  lastUsbConnected = usbNow;
}

// --- buttons: none exist ------------------------------------------------------

bool HalGPIO::isPressed(uint8_t) const { return false; }
bool HalGPIO::wasPressed(uint8_t) const { return false; }
bool HalGPIO::wasAnyPressed() const { return false; }
bool HalGPIO::wasReleased(uint8_t) const { return false; }
bool HalGPIO::wasAnyReleased() const { return false; }
unsigned long HalGPIO::getHeldTime() const { return 0; }
unsigned long HalGPIO::getPowerButtonHeldTime() const { return 0; }
bool HalGPIO::rawInputActive() { return touchOpen && touchDevice.isContactDown(); }

// --- home key: an X4 Pro control ---------------------------------------------

bool HalGPIO::hasHomeKey() const { return false; }
bool HalGPIO::wasHomeKeyTapped() const { return false; }
bool HalGPIO::wasHomeKeyLongPressed() const { return false; }

// --- touch --------------------------------------------------------------------

bool HalGPIO::hasTouch() const { return touchOpen; }

bool HalGPIO::wasTouchTap(float& nx, float& ny) const {
  if (frameGesture.kind != crosspoint::kindle::Gesture::Tap) {
    return false;
  }
  nx = frameGesture.nx;
  ny = frameGesture.ny;
  return true;
}

bool HalGPIO::wasTouchDown(float& nx, float& ny) const {
  // The classifier reports completed gestures, not the raw contact edge, so
  // the honest answer is the start of whatever gesture completed this frame.
  if (!frameGesture) {
    return false;
  }
  nx = frameGesture.nx;
  ny = frameGesture.ny;
  return true;
}

bool HalGPIO::wasTouchReleased() const {
  // Tap and swipe both fire on release; a long press fires while the finger is
  // still down and deliberately swallows its own release, so it is excluded.
  return frameGesture.kind == crosspoint::kindle::Gesture::Tap ||
         frameGesture.kind == crosspoint::kindle::Gesture::Swipe;
}

bool HalGPIO::isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) const {
  if (!touchDevice.isContactDown()) {
    return false;
  }
  // A contact is in progress; where it will end is not known yet, so report
  // where the last completed gesture began rather than inventing a position.
  nx = frameGesture.nx;
  ny = frameGesture.ny;
  heldMs = frameGesture.heldMs;
  return true;
}

bool HalGPIO::isTouchHeldAt(float& nx, float& ny) const {
  if (!touchDevice.isContactDown()) {
    return false;
  }
  nx = frameGesture.nx;
  ny = frameGesture.ny;
  return true;
}

bool HalGPIO::wasTouchLongPress(float& nx, float& ny) const {
  if (frameGesture.kind != crosspoint::kindle::Gesture::LongPress) {
    return false;
  }
  nx = frameGesture.nx;
  ny = frameGesture.ny;
  return true;
}

bool HalGPIO::wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const {
  if (frameGesture.kind != crosspoint::kindle::Gesture::Swipe) {
    return false;
  }
  nxStart = frameGesture.nx;
  nyStart = frameGesture.ny;
  nxEnd = frameGesture.nxEnd;
  nyEnd = frameGesture.nyEnd;
  return true;
}

void HalGPIO::suppressTouchContact() { touchDevice.suppressContact(); }

unsigned long HalGPIO::lastTouchHeldMs() const { return frameGesture.heldMs; }

bool HalGPIO::wasTouchActivity() const { return static_cast<bool>(frameGesture) || touchDevice.isContactDown(); }

// --- power and USB ------------------------------------------------------------

void HalGPIO::setSharedConfirmPowerShortPressEmitsPower(bool) {}

bool HalGPIO::verifyPowerButtonWakeup() { return false; }

bool HalGPIO::coldBootImpliesPowerButton() const { return false; }

bool HalGPIO::isUsbConnected() const {
  // The charger's presence is visible through sysfs. Reading it rather than
  // guessing matters: CrossPoint hides the "safe to unplug" affordances when
  // it thinks there is no host.
  FILE* f = std::fopen("/sys/class/power_supply/usb/online", "r");
  if (f == nullptr) {
    return false;
  }
  int online = 0;
  const int read = std::fscanf(f, "%d", &online);
  std::fclose(f);
  return read == 1 && online != 0;
}

bool HalGPIO::wasUsbStateChanged() const {
  // const in the interface, so the edge is detected against the state update()
  // last recorded rather than by sampling and mutating here.
  return usbStateChanged;
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  // The kernel suspends and resumes the device; this process is simply
  // descheduled and rescheduled, and never learns why.
  return WakeupReason::Other;
}

// Same as HalDisplayKindle.cpp: guarding out the ESP32 implementation took
// this global with it.
HalGPIO gpio;

#endif  // FREEINK_DEVICE_KINDLE
