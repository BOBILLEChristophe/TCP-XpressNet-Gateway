#pragma once
#include <Arduino.h>

// Clavier 3x4 : renvoie la touche PHYSIQUE ('0'..'9', '*', '#').
// Retourne '\0' si aucun nouvel appui. Un seul evenement par appui.
// La logique F0..F9 et #chiffre# est geree dans mouse.cpp.
namespace MouseKeypad {
void begin();
char poll();
}
