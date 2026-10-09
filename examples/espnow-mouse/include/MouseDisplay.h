#pragma once

#include "MouseState.h"

namespace MouseDisplay {
  void begin();
  void show(const MouseState &state);
}
