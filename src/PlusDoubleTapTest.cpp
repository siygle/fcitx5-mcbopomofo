// SPDX-License-Identifier: MIT
#include "PlusDoubleTap.h"
#include "gtest/gtest.h"

namespace McBopomofo::Plus {

// Helpers: watched key press/release at a time; other key press/release.
struct Tapper {
  DoubleTap d;
  bool down(int64_t t) { return d.onKey(true, false, t); }
  bool up(int64_t t) { return d.onKey(true, true, t); }
  bool other(int64_t t) { return d.onKey(false, false, t) | d.onKey(false, true, t); }
};

TEST(PlusDoubleTap, TwoQuickTapsFireOnce) {
  Tapper t;
  EXPECT_FALSE(t.down(0));
  EXPECT_FALSE(t.up(50));
  EXPECT_FALSE(t.down(200));
  EXPECT_TRUE(t.up(250));
  // A third tap starts over; it does not fire again on its own.
  EXPECT_FALSE(t.down(300));
  EXPECT_FALSE(t.up(350));
}

TEST(PlusDoubleTap, SlowSecondTapDoesNotFire) {
  Tapper t;
  t.down(0); t.up(50);
  EXPECT_FALSE(t.down(500));  // 450 ms after the first release > 300 ms.
  EXPECT_FALSE(t.up(550));
  // But it counts as a new first tap.
  t.down(600);
  EXPECT_TRUE(t.up(650));
}

TEST(PlusDoubleTap, UsingAsModifierIsNotATap) {
  Tapper t;
  // Shift held while typing a letter, twice: never a double tap.
  t.down(0); t.other(20); t.up(40);
  t.down(100); t.other(120);
  EXPECT_FALSE(t.up(140));
}

TEST(PlusDoubleTap, KeyBetweenTapsCancels) {
  Tapper t;
  t.down(0); t.up(50);
  t.other(100);
  t.down(150);
  EXPECT_FALSE(t.up(200));
}

TEST(PlusDoubleTap, LongHoldIsNotATap) {
  Tapper t;
  t.down(0); t.up(50);
  t.down(100);
  EXPECT_FALSE(t.up(900));  // Held 800 ms.
}

TEST(PlusDoubleTap, StrayReleaseIgnoredAndWindowConfigurable) {
  Tapper t;
  EXPECT_FALSE(t.up(0));  // Release without press (e.g. focus change).
  t.d.setWindowMs(600);
  t.down(100); t.up(150);
  t.down(650);  // 500 ms later: inside the 600 ms window.
  EXPECT_TRUE(t.up(700));
}

}  // namespace McBopomofo::Plus
