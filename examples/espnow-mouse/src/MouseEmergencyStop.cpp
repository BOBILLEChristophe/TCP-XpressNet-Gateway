#include "MouseEmergencyStop.h"

namespace {
constexpr uint8_t PIN_ESTOP = 32;
constexpr uint32_t DEBOUNCE_MS = 30;

bool lastRawState = HIGH;
bool stableState = HIGH;
uint32_t lastChangeMs = 0;
}

namespace MouseEmergencyStop {

void begin() {
  pinMode(PIN_ESTOP, INPUT_PULLUP);
  lastRawState = digitalRead(PIN_ESTOP);
  stableState = lastRawState;
  lastChangeMs = millis();
  Serial.println("Arret d'urgence : GPIO32 / GND (INPUT_PULLUP)");
}

bool poll() {
  const bool rawState = digitalRead(PIN_ESTOP);

  if (rawState != lastRawState) {
    lastRawState = rawState;
    lastChangeMs = millis();
  }

  if (rawState != stableState &&
      millis() - lastChangeMs >= DEBOUNCE_MS) {
    stableState = rawState;
    return stableState == LOW; // front d'appui uniquement
  }
  return false;
}

bool isHeld() {
  return stableState == LOW;
}

} // namespace MouseEmergencyStop
