#pragma once
#include <Arduino.h>
#include "MouseState.h"

// Liste fixe dans LocoManager.cpp, etats volatils independants par locomotive.
// Aucune ecriture Flash : une mise sous tension remet les vitesses locales a 0.
class LocoManager {
 public:
  static constexpr uint8_t MAX_SLOTS = 8;

  void begin();
  bool configured(uint8_t slot) const;
  bool select(uint8_t slot);
  MouseState &current();
  const MouseState &current() const;
  MouseState *findByAddress(uint16_t address);

 private:
  MouseState locos_[MAX_SLOTS] = {};
  uint8_t selectedIndex_ = 0;
};
