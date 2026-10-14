#pragma once

#include <Arduino.h>

namespace MouseEncoder {

struct Event {
  int8_t step = 0;    // +1 horaire, -1 antihoraire
  bool pressed = false;
};

void begin();
Event poll();

// Progression 0..100 : 10 premiers crans jusqu'a 12,
// 15 suivants +3, puis +5 (dernier palier limite a 100).
uint8_t nextSpeed(uint8_t currentSpeed, int8_t step);

} // namespace MouseEncoder
