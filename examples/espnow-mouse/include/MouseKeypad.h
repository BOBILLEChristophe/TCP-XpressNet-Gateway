#pragma once
#include <Arduino.h>

// Clavier matriciel 4 lignes x 3 colonnes (12 touches).
// Les huit premieres touches (1..8) commandent F0..F7.
// poll() renvoie -1 en l'absence de nouvel appui, sinon 0..7.
// Les touches 9, *, 0 et # sont reservees pour la suite.
namespace MouseKeypad {
void begin();
int8_t poll();
}
