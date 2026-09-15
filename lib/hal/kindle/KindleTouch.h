#pragma once

// Touch for the Kindle, split in two on purpose.
//
// TouchClassifier turns a stream of contact samples into gestures and knows
// nothing about file descriptors, so the host tests can drive it with
// synthetic sequences. KindleTouchDevice is the thin evdev half that cannot be
// tested without the hardware.
//
// Everything below is shaped by what the panel actually sends, captured with
// tools/kindle/inputprobe.cpp on the device:
//
//   /dev/input/event0  "zforce2"        Neonode zForce infrared
//     ABS_MT_SLOT          0..1         protocol B, two contacts maximum
//     ABS_MT_POSITION_X    0..599       already in panel coordinates
//     ABS_MT_POSITION_Y    0..799       ditto: no rotation, no mirroring
//     ABS_MT_TRACKING_ID   >=0 down, -1 up
//     BTN_TOUCH, BTN_TOOL_FINGER
//
// Two things from that capture drive the design:
//
// 1. Coordinates need no transform. A tap at the top-left read (39, 26) and
//    one at the bottom-right read (571, 754), even though the display side
//    reports rotation 3. Assuming a transform was needed because of that
//    rotation would have flipped the whole screen.
//
// 2. A stationary finger emits almost nothing. The probe's two-second hold
//    produced exactly one move event between touch-down and lift. So a long
//    press CANNOT be found by waiting for input: blocking on poll() would only
//    report it once the finger left, which is exactly too late. tick() exists
//    for that, and must be called on a timer whether or not events arrived.

#include <cstdint>

namespace crosspoint::kindle {

enum class Gesture : uint8_t {
  None,
  // Fires on lift: brief contact that stayed put.
  Tap,
  // Fires WHILE the finger is still down, once the hold threshold passes.
  // The rest of that contact is then suppressed, so the eventual lift does
  // not also read as a tap and dismiss whatever the long press opened.
  LongPress,
  // Fires on lift: contact that travelled far enough to be a drag.
  Swipe,
};

struct GestureResult {
  Gesture kind = Gesture::None;
  // Normalised to 0..1 so callers never see panel pixels. For a swipe, start
  // is where the finger landed and end is where it left.
  float nx = 0.0F;
  float ny = 0.0F;
  float nxEnd = 0.0F;
  float nyEnd = 0.0F;
  uint32_t heldMs = 0;

  explicit operator bool() const { return kind != Gesture::None; }
};

// Thresholds. Pixel values are in panel units on a 600x800 screen at 167ppi;
// a finger pad is roughly 40px across on this panel, which is what sets the
// slop.
struct TouchTuning {
  // Movement under this still counts as "stayed put", for both tap and the
  // long press. Below a finger's own wobble and there would be no taps.
  uint16_t slopPx = 20;
  // Past this, a contact is a drag rather than a tap.
  uint16_t swipeMinPx = 45;
  // Contact held this long without travelling is a long press.
  uint32_t longPressMs = 550;
  // A contact longer than this is not a tap even if it never moved. Prevents a
  // slow, careful press from being reported as both a long press and a tap.
  uint32_t tapMaxMs = 500;
};

class TouchClassifier {
 public:
  TouchClassifier(const uint16_t width, const uint16_t height, const TouchTuning tuning = {})
      : w(width), h(height), cfg(tuning) {}

  // Feed one contact sample. `down` false ends the contact.
  GestureResult onSample(uint16_t x, uint16_t y, bool down, uint32_t nowMs);

  // Call this regularly even when no events arrived: on this panel a held
  // finger is silent, and this is the only thing that can notice it.
  GestureResult tick(uint32_t nowMs);

  bool isContactDown() const { return down; }

  // Drop the rest of the current contact without reporting anything. Used
  // after a long press fires, and available to callers that have consumed a
  // contact some other way.
  void suppressContact() { suppressed = true; }

 private:
  GestureResult make(Gesture kind, uint32_t nowMs) const;
  bool travelledBeyond(uint16_t px) const;

  uint16_t w;
  uint16_t h;
  TouchTuning cfg;

  bool down = false;
  bool suppressed = false;
  bool longPressFired = false;
  uint16_t startX = 0;
  uint16_t startY = 0;
  uint16_t curX = 0;
  uint16_t curY = 0;
  uint32_t startMs = 0;
};

// The evdev half. Opens the touch device, accumulates the per-SYN axis deltas
// the driver sends (it only reports axes that changed), and feeds the
// classifier.
class KindleTouchDevice {
 public:
  KindleTouchDevice() = default;
  ~KindleTouchDevice();

  KindleTouchDevice(const KindleTouchDevice&) = delete;
  KindleTouchDevice& operator=(const KindleTouchDevice&) = delete;

  // Finds the touchscreen by capability (ABS_MT_POSITION_X/Y) rather than by
  // name, so this does not break on a Kindle whose controller is not a zForce.
  bool begin(uint16_t panelWidth, uint16_t panelHeight, TouchTuning tuning = {});
  void end();
  bool isOpen() const { return fd >= 0; }

  // Drains pending events and returns the first gesture they complete, then
  // gives the classifier a tick so a silent hold is still noticed.
  // `timeoutMs` 0 polls without blocking.
  GestureResult update(int timeoutMs = 0);

  void suppressContact() { classifier.suppressContact(); }
  bool isContactDown() const { return classifier.isContactDown(); }

 private:
  // Reopen the evdev node after the descriptor has gone bad.
  //
  // A suspend takes the touch controller down with the rest of the machine,
  // and the descriptor this process holds does not survive it: poll() starts
  // returning POLLERR or POLLHUP and read() fails, after which update() drains
  // nothing and reports nothing, forever and without a word. The reader looks
  // alive and correct and simply ignores every touch, which is exactly what it
  // did. Nothing here is told when that happens, so instead of being told, the
  // failure is noticed and repaired where it shows up.
  bool reopen();

  int fd = -1;
  // Kept so reopen() can rebuild the classifier the way begin() did.
  uint16_t openedWidth = 600;
  uint16_t openedHeight = 800;
  TouchTuning openedTuning{};
  // Rate limit: a device that is genuinely gone must not turn every frame into
  // a directory scan.
  unsigned long nextReopenAtMs = 0;
  TouchClassifier classifier{600, 800};
  // Accumulated across a SYN boundary: the driver sends only what changed.
  int32_t pendingX = 0;
  int32_t pendingY = 0;
  bool pendingDown = false;
  bool haveContact = false;
};

}  // namespace crosspoint::kindle
