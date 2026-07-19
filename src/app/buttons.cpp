// src/app/buttons.cpp
#include "app/buttons.hpp"

namespace app {

constexpr int Buttons::kPins[3];

void Buttons::begin() {
  for (int i = 0; i < 3; ++i) {
    pinMode(kPins[i],
            INPUT);  // 34/35/39 can't use INPUT_PULLUP; board pulls up
    lastRead_[i] = stable_[i] = true;
    tChange_[i] = millis();
  }
}

Btn Buttons::poll() {
  Btn event = Btn::None;
  uint32_t now = millis();
  for (int i = 0; i < 3; ++i) {
    bool r = digitalRead(kPins[i]);  // HIGH idle, LOW pressed
    if (r != lastRead_[i]) {
      lastRead_[i] = r;
      tChange_[i] = now;
    }
    if (now - tChange_[i] >= kDebounceMs && stable_[i] != lastRead_[i]) {
      stable_[i] = lastRead_[i];
      if (stable_[i] == false && event == Btn::None) {  // falling edge = press
        event = (i == 0) ? Btn::Left : (i == 1) ? Btn::Mid : Btn::Right;
      }
    }
  }
  return event;
}

}  // namespace app
