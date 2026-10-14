#pragma once
#include <Arduino.h>

// Etat local propre a chaque locomotive.
// Il ne s'agit pas d'un retour d'etat de la centrale.
struct MouseState {
  uint8_t slot = 0;        // Affichage 1..10 ; selection #1#..#9# / #0#
  uint16_t address = 0;    // 0 = emplacement non configure
  uint8_t speed = 0;       // Echelle commune 0..100
  bool forward = true;
  bool functions[10] = {}; // F0..F9
};
