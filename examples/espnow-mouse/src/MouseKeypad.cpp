#include "MouseKeypad.h"

namespace {
// GPIO libres sur la souris ESP32 classique.
// Ne pas utiliser les broches de l'OLED, de l'encodeur,
// de l'arret d'urgence ou de la mesure batterie.
constexpr uint8_t ROW_PINS[4] = {13, 14, 4, 33};
constexpr uint8_t COL_PINS[3] = {18, 19, 23};
constexpr uint32_t DEBOUNCE_MS = 30;
constexpr int8_t NO_KEY = -1;

int8_t lastRawKey = NO_KEY;
int8_t stableKey = NO_KEY;
uint32_t lastChangeMs = 0;

// Une seule ligne a LOW a la fois. Les autres restent en haute impedance.
// Ainsi deux touches ne relient jamais deux sorties opposees.
int8_t scanKey() {
  int8_t found = NO_KEY;
  bool multiple = false;

  for (uint8_t row = 0; row < 4; ++row) {
    pinMode(ROW_PINS[row], OUTPUT);
    digitalWrite(ROW_PINS[row], LOW);
    delayMicroseconds(5);

    for (uint8_t col = 0; col < 3; ++col) {
      if (digitalRead(COL_PINS[col]) == LOW) {
        const int8_t key = static_cast<int8_t>(row * 3 + col);
        if (found != NO_KEY) multiple = true;
        else found = key;
      }
    }

    pinMode(ROW_PINS[row], INPUT); // haute impedance
  }

  // Evite les bascules ambigues en cas de touches multiples / ghosting.
  return multiple ? NO_KEY : found;
}
} // namespace

namespace MouseKeypad {
void begin() {
  for (uint8_t pin : ROW_PINS) pinMode(pin, INPUT);
  for (uint8_t pin : COL_PINS) pinMode(pin, INPUT_PULLUP);

  lastRawKey = scanKey();
  stableKey = lastRawKey; // aucun appui artificiel au demarrage
  lastChangeMs = millis();

  Serial.println("Clavier 3x4 : lignes 13,14,4,33 / colonnes 18,19,23");
  Serial.println("Touches 1..8 : F0..F7 ; # numero # = selection loco");
}

int8_t poll() {
  const int8_t rawKey = scanKey();
  const uint32_t now = millis();

  if (rawKey != lastRawKey) {
    lastRawKey = rawKey;
    lastChangeMs = now;
  }

  if (rawKey != stableKey && now - lastChangeMs >= DEBOUNCE_MS) {
    stableKey = rawKey;
    // Seul le front d'appui est pris en compte ; relacher n'envoie rien.
    if (stableKey >= 0 && stableKey < 12) return stableKey;
  }
  return NO_KEY;
}
} // namespace MouseKeypad
