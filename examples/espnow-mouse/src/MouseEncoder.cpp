#include "MouseEncoder.h"

#include "soc/gpio_struct.h"  // lecture directe GPIO25/26, sure dans l'ISR ESP32

namespace {

constexpr uint8_t PIN_A = 25;
constexpr uint8_t PIN_B = 26;
constexpr uint8_t PIN_SW = 27;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;

// Meme decodeur quadrature que la version validee :
// quatre transitions par cran, validation au retour a A=1 et B=1.
// DRAM_ATTR : table accessible pendant une interruption IRAM.
DRAM_ATTR const int8_t TRANSITION_TABLE[16] = {
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

portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;
volatile int16_t pendingSteps = 0;
uint8_t previousAB = 3;
int8_t accumulator = 0;

bool lastRawButton = HIGH;
bool stableButton = HIGH;
uint32_t lastButtonChangeMs = 0;

void IRAM_ATTR onEncoderChange() {
  // Les deux GPIO sont dans le registre GPIO.in (GPIO 0..31 de l'ESP32).
  // Aucun appel I2C, Serial, allocation ou Wi-Fi dans l'interruption.
  const uint32_t levels = GPIO.in;
  const uint8_t currentAB = static_cast<uint8_t>(
      (((levels >> PIN_A) & 1U) << 1) | ((levels >> PIN_B) & 1U));

  portENTER_CRITICAL_ISR(&encoderMux);
  if (currentAB != previousAB) {
    const uint8_t index = (previousAB << 2) | currentAB;
    const int8_t movement = TRANSITION_TABLE[index];
    if (movement != 0) {
      accumulator += movement;
    } else if ((previousAB ^ currentAB) == 3) {
      // Saut impossible : abandon de la sequence incomplete.
      accumulator = 0;
    }
    previousAB = currentAB;

    if (currentAB == 3) {
      // Limite de securite : 100 crans en attente couvrent toute l'echelle.
      if (accumulator >= 4 && pendingSteps < 100) ++pendingSteps;
      else if (accumulator <= -4 && pendingSteps > -100) --pendingSteps;
      accumulator = 0;
    }
  }
  portEXIT_CRITICAL_ISR(&encoderMux);
}

} // namespace

namespace MouseEncoder {

void begin() {
  pinMode(PIN_A, INPUT_PULLUP);
  pinMode(PIN_B, INPUT_PULLUP);
  pinMode(PIN_SW, INPUT_PULLUP);

  const uint32_t levels = GPIO.in;
  previousAB = static_cast<uint8_t>(
      (((levels >> PIN_A) & 1U) << 1) | ((levels >> PIN_B) & 1U));
  accumulator = 0;
  pendingSteps = 0;

  lastRawButton = digitalRead(PIN_SW);
  stableButton = lastRawButton;
  lastButtonChangeMs = millis();

  attachInterrupt(digitalPinToInterrupt(PIN_A), onEncoderChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_B), onEncoderChange, CHANGE);

  Serial.println("Encodeur : A=25 B=26 (interruptions), SW=27");
}

Event poll() {
  Event event;

  // Recuperation atomique de TOUS les crans enregistres pendant
  // l'affichage OLED ou les transmissions ESP-NOW.
  portENTER_CRITICAL(&encoderMux);
  event.step = pendingSteps;
  pendingSteps = 0;
  portEXIT_CRITICAL(&encoderMux);

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

uint8_t nextSpeed(uint8_t currentSpeed, int16_t steps) {
  while (steps > 0 && currentSpeed < 100) {
    for (uint8_t speed : SPEED_STEPS) {
      if (speed > currentSpeed) {
        currentSpeed = speed;
        break;
      }
    }
    --steps;
  }

  while (steps < 0 && currentSpeed > 0) {
    for (int i = static_cast<int>(sizeof(SPEED_STEPS)) - 1; i >= 0; --i) {
      if (SPEED_STEPS[i] < currentSpeed) {
        currentSpeed = SPEED_STEPS[i];
        break;
      }
    }
    ++steps;
  }
  return currentSpeed;
}

} // namespace MouseEncoder
