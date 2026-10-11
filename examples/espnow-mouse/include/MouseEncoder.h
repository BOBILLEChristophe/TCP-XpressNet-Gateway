#pragma once

#include <Arduino.h>

namespace MouseEncoder {

struct Event {
  int16_t step = 0;  // nombre de crans cumules depuis le dernier poll()
  bool pressed = false;
};

void begin();
Event poll();

// Progression 0..100, avec application de tous les crans accumules.
uint8_t nextSpeed(uint8_t currentSpeed, int16_t steps);

} // namespace MouseEncoder
