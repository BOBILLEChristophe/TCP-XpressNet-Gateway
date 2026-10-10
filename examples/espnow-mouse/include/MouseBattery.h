#pragma once

#include <Arduino.h>

// Tension reelle de la 18650, mesuree apres BMS et AVANT le step-up.
struct BatteryReading {
  float voltage = 0.0f;
  uint8_t percent = 0;
  bool low = false;
};

namespace MouseBattery {
  void begin();
  // Acquisition non bloquante : retourne true apres une nouvelle moyenne.
  bool poll();
  BatteryReading reading();
}
