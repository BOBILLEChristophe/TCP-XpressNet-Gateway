#pragma once
#include <Arduino.h>

// Consignes locales : ne constituent pas un retour d'etat de la centrale.
struct MouseState {
  uint8_t slot = 1;          // Loco #1, puis autres slots avec le futur clavier
  uint16_t address = 6400;
  uint8_t speed = 0;         // Echelle commune 0..100
  bool forward = true;
  bool functions[8] = {};    // F0..F7 : affichage local uniquement pour l'instant
};
