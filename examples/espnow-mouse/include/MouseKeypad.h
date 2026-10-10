#pragma once
#include <Arduino.h>

// Clavier matriciel 4 lignes x 3 colonnes (12 touches).
// Les huit premieres touches (1..8) commandent F0..F7.
// poll() renvoie -1 en l'absence de nouvel appui, sinon 0..11.
// Touches 1..8 : F0..F7 en mode normal ; # : selection, * : annulation.
// Positions 0..8 = touches 1..9, 9 = *, 10 = 0, 11 = #.
namespace MouseKeypad {
void begin();
int8_t poll();
}
