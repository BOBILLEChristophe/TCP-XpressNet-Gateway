#pragma once
#include <Arduino.h>

// Consignes locales (ne constituent pas un retour d'etat de la centrale).
struct MouseState {
  uint8_t slot = 1;          // L1, puis L2..L9 lorsque le clavier sera ajoute
  uint16_t address = 6400;
  uint8_t speed = 0;         // Echelle commune 0..100
  bool forward = true;
  bool functions[5] = {false, false, false, false, false};
};
