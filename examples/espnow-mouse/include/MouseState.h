#pragma once
#include <Arduino.h>

// Consignes locales de la souris (pas un retour d'etat de la centrale).
struct MouseState {
  uint16_t address = 6400;
  uint8_t speed = 0;
  bool forward = true;
  bool functions[4] = {false, false, false, false};
};
