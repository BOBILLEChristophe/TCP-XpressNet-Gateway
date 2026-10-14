#pragma once

#include "MouseState.h"
#include "MouseBattery.h"

namespace MouseDisplay {
  void begin();
  void show(const MouseState &state, const BatteryReading &battery);
}
