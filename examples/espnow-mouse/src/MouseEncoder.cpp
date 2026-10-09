#include "MouseEncoder.h"

namespace {

constexpr uint8_t PIN_A = 25;
constexpr uint8_t PIN_B = 26;
constexpr uint8_t PIN_SW = 27;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;

// Sequence validee sur l'encodeur reel : 4 transitions / cran,
// avec validation au retour a A=1, B=1.
constexpr int8_t TRANSITION_TABLE[16] = {
   0, -1, +1,  0,
  +1,  0,  0, -1,
  -1,  0,  0, +1,
   0, +1, -1,  0
};

constexpr uint8_t SPEED_STEPS[] = {
   0,  2,  3,  4,  5,  6,  7,  8,  9, 10, 12,
  15, 18, 21, 24, 27, 30, 33, 36, 39, 42,
  45, 48, 51, 54, 57,
  62, 67, 72, 77, 82, 87, 92, 97, 100
};

uint8_t previousAB = 3;
int8_t accumulator = 0;

bool lastRawButton = HIGH;
bool stableButton = HIGH;
uint32_t lastButtonChangeMs = 0;

} // namespace

namespace MouseEncoder {

void begin() {
  pinMode(PIN_A, INPUT_PULLUP);
  pinMode(PIN_B, INPUT_PULLUP);
  pinMode(PIN_SW, INPUT_PULLUP);

  previousAB = (digitalRead(PIN_A) << 1) | digitalRead(PIN_B);
  accumulator = 0;
  lastRawButton = digitalRead(PIN_SW);
  stableButton = lastRawButton;
  lastButtonChangeMs = millis();

  Serial.println("Encodeur : A=25 B=26 SW=27 (4 transitions/cr an)");
}

Event poll() {
  Event event;

  const uint8_t currentAB =
      (digitalRead(PIN_A) << 1) | digitalRead(PIN_B);

  if (currentAB != previousAB) {
    const uint8_t index = (previousAB << 2) | currentAB;
    const int8_t movement = TRANSITION_TABLE[index];

    if (movement != 0) {
      accumulator += movement;
    } else if ((previousAB ^ currentAB) == 3) {
      // Transition impossible : abandonner la sequence incomplete.
      accumulator = 0;
    }
    previousAB = currentAB;

    if (currentAB == 3) {
      if (accumulator >= 4) event.step = +1;
      else if (accumulator <= -4) event.step = -1;
      accumulator = 0;
    }
  }

  const bool rawButton = digitalRead(PIN_SW);
  if (rawButton != lastRawButton) {
    lastButtonChangeMs = millis();
    lastRawButton = rawButton;
  }
  if (rawButton != stableButton &&
      millis() - lastButtonChangeMs >= BUTTON_DEBOUNCE_MS) {
    stableButton = rawButton;
    if (stableButton == LOW) event.pressed = true;
  }

  return event;
}

uint8_t nextSpeed(uint8_t currentSpeed, int8_t step) {
  if (step > 0) {
    for (uint8_t speed : SPEED_STEPS) {
      if (speed > currentSpeed) return speed;
    }
    return 100;
  }

  if (step < 0) {
    for (int i = static_cast<int>(sizeof(SPEED_STEPS)) - 1; i >= 0; --i) {
      if (SPEED_STEPS[i] < currentSpeed) return SPEED_STEPS[i];
    }
    return 0;
  }
  return currentSpeed;
}

} // namespace MouseEncoder
