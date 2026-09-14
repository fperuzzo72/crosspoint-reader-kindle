// Gesture classification, kept free of file descriptors so the host tests can
// drive it with synthetic contact sequences. The evdev half lives in
// KindleTouchDevice.cpp and cannot be tested without the panel.

#include "KindleTouch.h"

namespace crosspoint::kindle {
namespace {

uint32_t squaredDistance(const uint16_t ax, const uint16_t ay, const uint16_t bx, const uint16_t by) {
  const int32_t dx = static_cast<int32_t>(ax) - static_cast<int32_t>(bx);
  const int32_t dy = static_cast<int32_t>(ay) - static_cast<int32_t>(by);
  return static_cast<uint32_t>(dx * dx + dy * dy);
}

float normalise(const uint16_t v, const uint16_t extent) {
  // The axis maxima are inclusive (0..599 on a 600px panel), so the divisor is
  // extent - 1 and the far edge lands exactly on 1.0.
  if (extent <= 1) {
    return 0.0F;
  }
  return static_cast<float>(v) / static_cast<float>(extent - 1);
}

}  // namespace

bool TouchClassifier::travelledBeyond(const uint16_t px) const {
  return squaredDistance(startX, startY, curX, curY) > static_cast<uint32_t>(px) * px;
}

GestureResult TouchClassifier::make(const Gesture kind, const uint32_t nowMs) const {
  GestureResult r;
  r.kind = kind;
  r.nx = normalise(startX, w);
  r.ny = normalise(startY, h);
  r.nxEnd = normalise(curX, w);
  r.nyEnd = normalise(curY, h);
  r.heldMs = nowMs - startMs;
  // A tap and a long press are one point, not a segment: report where the
  // finger is rather than where it landed, so a press that drifted a few
  // pixels still acts on what is under the finger now.
  if (kind == Gesture::Tap || kind == Gesture::LongPress) {
    r.nx = r.nxEnd;
    r.ny = r.nyEnd;
  }
  return r;
}

GestureResult TouchClassifier::onSample(const uint16_t x, const uint16_t y, const bool isDown, const uint32_t nowMs) {
  if (isDown && !down) {
    down = true;
    suppressed = false;
    longPressFired = false;
    startX = curX = x;
    startY = curY = y;
    startMs = nowMs;
    return {};
  }

  if (isDown) {
    curX = x;
    curY = y;
    // A moving finger is not a long press, so let tick() find it; nothing to
    // report mid-contact.
    return tick(nowMs);
  }

  if (!down) {
    return {};
  }

  // Lift.
  curX = x;
  curY = y;
  down = false;

  if (suppressed) {
    suppressed = false;
    return {};
  }

  const uint32_t heldMs = nowMs - startMs;
  if (travelledBeyond(cfg.swipeMinPx)) {
    return make(Gesture::Swipe, nowMs);
  }
  if (heldMs <= cfg.tapMaxMs && !travelledBeyond(cfg.slopPx)) {
    return make(Gesture::Tap, nowMs);
  }
  // Held too long to be a tap but never travelled: the long press either
  // already fired (and suppressed this contact) or the hold threshold sits
  // above tapMaxMs and this landed in the gap. Either way, report nothing
  // rather than inventing a gesture the user did not make.
  return {};
}

GestureResult TouchClassifier::tick(const uint32_t nowMs) {
  if (!down || suppressed || longPressFired) {
    return {};
  }
  if (nowMs - startMs < cfg.longPressMs) {
    return {};
  }
  if (travelledBeyond(cfg.slopPx)) {
    return {};
  }

  longPressFired = true;
  // Swallow the remainder of this contact: without it the finger lifting would
  // also read as a tap, and that tap would dismiss whatever the long press
  // just opened.
  suppressed = true;
  return make(Gesture::LongPress, nowMs);
}

}  // namespace crosspoint::kindle
