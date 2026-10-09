#pragma once

#include <Arduino.h>

// Bouton d'arret d'urgence independant de l'encodeur.
// Retourne true une seule fois par appui valide.
namespace MouseEmergencyStop {
void begin();
bool poll();
bool isHeld();
}
