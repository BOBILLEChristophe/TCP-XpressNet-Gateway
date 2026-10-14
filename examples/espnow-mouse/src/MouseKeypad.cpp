#include "MouseKeypad.h"

namespace {
// Brochage valide sur la souris ESP32 classique : ne pas utiliser GPIO16/17.
constexpr uint8_t ROW_PINS[4] = {13, 14, 4, 33};
constexpr uint8_t COL_PINS[3] = {18, 19, 23};
constexpr uint32_t DEBOUNCE_MS = 30;
constexpr int8_t NO_KEY = -1;

// Index row*3+col : 0=1, 1=2, ..., 9=*, 10=0, 11=#.
constexpr char KEY_LABELS[13] = "123456789*0#";

int8_t lastRawKey = NO_KEY;
int8_t stableKey = NO_KEY;
uint32_t lastChangeMs = 0;

// Une seule ligne a LOW ; les autres restent en haute impedance.
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

    pinMode(ROW_PINS[row], INPUT);
  }
  return multiple ? NO_KEY : found;
}
} // namespace

namespace MouseKeypad {
void begin() {
  for (uint8_t pin : ROW_PINS) pinMode(pin, INPUT);
  for (uint8_t pin : COL_PINS) pinMode(pin, INPUT_PULLUP);

  lastRawKey = scanKey();
  stableKey = lastRawKey;
  lastChangeMs = millis();

  Serial.println("Clavier 3x4 : lignes 13,14,4,33 / colonnes 18,19,23");
  Serial.println("Touches 0..9 : F0..F9 ; #1#..#9# / #0# : selection locomotive");
}

char poll() {
  const int8_t rawKey = scanKey();
  const uint32_t now = millis();

  if (rawKey != lastRawKey) {
    lastRawKey = rawKey;
    lastChangeMs = now;
  }

  if (rawKey != stableKey && now - lastChangeMs >= DEBOUNCE_MS) {
    stableKey = rawKey;
    if (stableKey >= 0 && stableKey < 12) {
      return KEY_LABELS[stableKey];
    }
  }
  return '\0';
}
} // namespace MouseKeypad
