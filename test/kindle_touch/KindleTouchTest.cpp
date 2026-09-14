// Gesture classification, driven with the sequences the real panel produced.
//
// The coordinates here are not invented: they come from
// tools/kindle/inputprobe.cpp run on the device, so the tap positions are the
// ones a finger actually reported at the corners of a 600x800 zForce panel.

#include <gtest/gtest.h>

#include "KindleTouch.h"

namespace {

using crosspoint::kindle::Gesture;
using crosspoint::kindle::GestureResult;
using crosspoint::kindle::TouchClassifier;

constexpr uint16_t W = 600;
constexpr uint16_t H = 800;

TouchClassifier fresh() { return TouchClassifier(W, H); }

TEST(KindleTouch, TapAtTopLeftReportsNearOrigin) {
  // Captured: down (39,26), drifts to (41,30), up. ~120 ms.
  auto c = fresh();
  EXPECT_FALSE(c.onSample(39, 26, true, 1000));
  EXPECT_FALSE(c.onSample(41, 30, true, 1060));
  const GestureResult g = c.onSample(41, 30, false, 1120);

  ASSERT_EQ(g.kind, Gesture::Tap);
  EXPECT_NEAR(g.nx, 41.0F / 599.0F, 0.001F);
  EXPECT_NEAR(g.ny, 30.0F / 799.0F, 0.001F);
}

TEST(KindleTouch, TapAtBottomRightReportsNearOne) {
  // Captured: down (571,754). The far edge of each axis is the inclusive
  // maximum, so a corner tap must land close to 1.0 and never above it.
  auto c = fresh();
  c.onSample(571, 754, true, 2000);
  const GestureResult g = c.onSample(571, 754, false, 2100);

  ASSERT_EQ(g.kind, Gesture::Tap);
  EXPECT_GT(g.nx, 0.9F);
  EXPECT_GT(g.ny, 0.9F);
  EXPECT_LE(g.nx, 1.0F);
  EXPECT_LE(g.ny, 1.0F);
}

TEST(KindleTouch, NormalisationHitsExactlyOneAtTheFarEdge) {
  auto c = fresh();
  c.onSample(599, 799, true, 0);
  const GestureResult g = c.onSample(599, 799, false, 50);
  EXPECT_FLOAT_EQ(g.nx, 1.0F);
  EXPECT_FLOAT_EQ(g.ny, 1.0F);
}

TEST(KindleTouch, LeftToRightDragIsASwipeWithBothEnds) {
  auto c = fresh();
  c.onSample(100, 400, true, 0);
  for (uint16_t x = 110; x <= 500; x += 10) {
    EXPECT_FALSE(c.onSample(x, 400, true, 10U * ((x - 100) / 10)));
  }
  const GestureResult g = c.onSample(500, 400, false, 500);

  ASSERT_EQ(g.kind, Gesture::Swipe);
  EXPECT_NEAR(g.nx, 100.0F / 599.0F, 0.001F);     // where it landed
  EXPECT_NEAR(g.nxEnd, 500.0F / 599.0F, 0.001F);  // where it left
  EXPECT_NEAR(g.ny, g.nyEnd, 0.001F);
}

TEST(KindleTouch, HeldFingerLongPressesWithoutAnyFurtherEvents) {
  // This is the whole reason tick() exists. The probe's two-second hold sent
  // ONE move event between touch-down and lift, so a classifier that only ran
  // on input would report the long press when the finger left, which is too
  // late to be any use.
  auto c = fresh();
  c.onSample(331, 576, true, 0);

  EXPECT_FALSE(c.tick(100));
  EXPECT_FALSE(c.tick(400));

  const GestureResult g = c.tick(600);
  ASSERT_EQ(g.kind, Gesture::LongPress);
  EXPECT_NEAR(g.nx, 331.0F / 599.0F, 0.001F);
  EXPECT_GE(g.heldMs, 550U);
}

TEST(KindleTouch, LongPressFiresOnceAndSwallowsTheLift) {
  // Without suppression the finger coming up would also read as a tap, and
  // that tap would dismiss whatever the long press had just opened.
  auto c = fresh();
  c.onSample(300, 300, true, 0);
  ASSERT_EQ(c.tick(600).kind, Gesture::LongPress);

  EXPECT_FALSE(c.tick(700));                        // does not repeat
  EXPECT_FALSE(c.onSample(300, 300, false, 800));   // the lift is swallowed
}

TEST(KindleTouch, MovingFingerNeverLongPresses) {
  auto c = fresh();
  c.onSample(100, 400, true, 0);
  c.onSample(200, 400, true, 200);  // well past the slop
  EXPECT_FALSE(c.tick(900));
  EXPECT_EQ(c.onSample(200, 400, false, 1000).kind, Gesture::Swipe);
}

TEST(KindleTouch, SlowStationaryPressReportsNothingRatherThanInventingATap) {
  // Between tapMaxMs (500) and longPressMs (550) there is a deliberate gap. A
  // contact that ends inside it moved too little to be a swipe and lasted too
  // long to be a tap, so the honest answer is no gesture.
  auto c = fresh();
  c.onSample(300, 300, true, 0);
  const GestureResult g = c.onSample(300, 300, false, 520);
  EXPECT_EQ(g.kind, Gesture::None);
}

TEST(KindleTouch, ContactsAreIndependent) {
  auto c = fresh();
  c.onSample(300, 300, true, 0);
  ASSERT_EQ(c.tick(600).kind, Gesture::LongPress);
  c.onSample(300, 300, false, 700);

  // A long press must not poison the next contact.
  c.onSample(50, 50, true, 1000);
  EXPECT_EQ(c.onSample(50, 50, false, 1100).kind, Gesture::Tap);
}

TEST(KindleTouch, SuppressContactDropsTheRestOfIt) {
  auto c = fresh();
  c.onSample(300, 300, true, 0);
  c.suppressContact();
  EXPECT_FALSE(c.onSample(300, 300, false, 100));
  // And the following contact still works.
  c.onSample(10, 10, true, 200);
  EXPECT_EQ(c.onSample(10, 10, false, 260).kind, Gesture::Tap);
}

TEST(KindleTouch, LiftWithoutDownIsIgnored) {
  auto c = fresh();
  EXPECT_FALSE(c.onSample(100, 100, false, 0));
  EXPECT_FALSE(c.tick(1000));
}

}  // namespace
