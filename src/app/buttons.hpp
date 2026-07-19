// src/app/buttons.hpp
// The three front buttons on the T5-4.7 (classic ESP32): GPIO34/35/39.
// These are input-only pins with no internal pull-ups — the board provides
// external pull-ups, so they read HIGH idle and LOW when pressed.
//
// Mapping (per the navigation spec): leftmost = previous page,
// second-from-left = jump to main page, rightmost = next page.
#pragma once
#include <Arduino.h>

namespace app {

enum class Btn : uint8_t { None, Left, Mid, Right };

class Buttons {
 public:
  void begin();
  Btn poll();  // edge-triggered, debounced; returns one press per physical push

 private:
  static constexpr int kPins[3] = {34, 35, 39};  // Left, Mid, Right
  static constexpr uint32_t kDebounceMs = 35;
  bool lastRead_[3] = {true, true, true};
  bool stable_[3] = {true, true, true};
  uint32_t tChange_[3] = {0, 0, 0};
};

}  // namespace app
