// SPDX-License-Identifier: MIT
// Double-tap detection for Plus shortcuts (e.g. tap Right Shift twice).
#ifndef SRC_PLUSDOUBLETAP_H_
#define SRC_PLUSDOUBLETAP_H_

#include <cstdint>

namespace McBopomofo::Plus {

// Feed every key press/release (not only the watched key). A "tap" is a press
// of the watched key followed by its release with no other key in between;
// holding it as a modifier (Shift+A) is not a tap, nor is a press held longer
// than the window. Fires when the second tap is pressed within the window
// after the first tap was released. Pure logic, no fcitx dependency, so it is unit tested directly.
class DoubleTap {
 public:
  void setWindowMs(int ms) { windowMs_ = ms; }

  // Returns true on the release that completes a double tap.
  bool onKey(bool isWatchedKey, bool isRelease, int64_t nowMs) {
    if (!isWatchedKey) {
      // Any other key (press or release) breaks a tap in progress, and a
      // press also cancels a pending first tap.
      pressed_ = false;
      if (!isRelease) firstTapMs_ = -1;
      return false;
    }
    if (!isRelease) {
      pressed_ = true;
      pressMs_ = nowMs;
      return false;
    }
    if (!pressed_) return false;
    pressed_ = false;
    // A long hold is not a tap either.
    if (nowMs - pressMs_ > windowMs_) {
      firstTapMs_ = -1;
      return false;
    }
    if (firstTapMs_ >= 0 && pressMs_ - firstTapMs_ <= windowMs_) {
      firstTapMs_ = -1;
      return true;
    }
    firstTapMs_ = nowMs;
    return false;
  }

  void reset() {
    pressed_ = false;
    firstTapMs_ = -1;
  }

 private:
  int windowMs_ = 300;
  bool pressed_ = false;
  int64_t pressMs_ = 0;
  int64_t firstTapMs_ = -1;
};

}  // namespace McBopomofo::Plus
#endif
