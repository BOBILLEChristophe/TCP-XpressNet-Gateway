#include "MousePower.h"

#include <Arduino.h>

namespace
{
  constexpr uint8_t PIN_BUTTON = 35;
  constexpr uint8_t PIN_LED = 5;
  constexpr uint32_t DEBOUNCE_MS = 30;
  constexpr uint32_t STATUS_TIMEOUT_MS = 4500; // passerelle : retour toutes les 1500 ms
  constexpr uint32_t REQUEST_TIMEOUT_MS = 4000;
  constexpr uint32_t BLINK_MS = 350;

  enum class State : uint8_t
  {
    UNKNOWN,
    OFF,
    ON
  };
  State state = State::UNKNOWN;
  bool lastRaw = HIGH;
  bool stable = HIGH;
  uint32_t lastChangeMs = 0;
  uint32_t lastStatusMs = 0;
  bool pending = false;
  bool targetOn = false;
  uint32_t requestMs = 0;
  bool displayChanged = true;
}

namespace MousePower
{
  void begin()
  {
    pinMode(PIN_BUTTON, INPUT);
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH); // LED éteinte au démarrage
    lastRaw = digitalRead(PIN_BUTTON);
    stable = lastRaw;
    lastChangeMs = millis();
    state = State::UNKNOWN;
    pending = false;
    displayChanged = true;
    Serial.println("POWER souris : poussoir GPIO35 / GND, LED GPIO5");
  }

  void confirm(bool powerOn)
  {
    const State next = powerOn ? State::ON : State::OFF;
    if (next != state)
    {
      state = next;
      displayChanged = true;
      Serial.printf("POWER confirme par passerelle : %s\n", powerOn ? "ON" : "OFF");
    }
    lastStatusMs = millis();
    if (pending && targetOn == powerOn)
    {
      pending = false;
      displayChanged = true;
      Serial.println("POWER : nouvelle position confirmee");
    }
  }

  bool desiredPowerOn()
  {
    return state == State::OFF;
  }

  void markRequested()
  {
    if (state == State::UNKNOWN)
      return;
    pending = true;
    targetOn = (state == State::OFF);
    requestMs = millis();
    displayChanged = true;
  }

  bool poll()
  {
    const uint32_t now = millis();

    if (state != State::UNKNOWN && now - lastStatusMs > STATUS_TIMEOUT_MS)
    {
      state = State::UNKNOWN;
      pending = false;
      displayChanged = true;
      Serial.println("POWER : liaison sans retour d'etat, position inconnue");
    }
    if (pending && now - requestMs > REQUEST_TIMEOUT_MS)
    {
      pending = false;
      displayChanged = true;
      Serial.println("POWER : delai de confirmation depasse, etat precedent conserve");
    }

    const bool ledOn = state == State::ON ||
                       (state == State::UNKNOWN && ((now / BLINK_MS) % 2 == 0));
    digitalWrite(PIN_LED, ledOn ? LOW : HIGH);

    const bool raw = digitalRead(PIN_BUTTON);
    if (raw != lastRaw)
    {
      lastRaw = raw;
      lastChangeMs = now;
    }
    if (raw != stable && now - lastChangeMs >= DEBOUNCE_MS)
    {
      stable = raw;
      if (stable == LOW)
      {
        if (state != State::UNKNOWN && !pending)
          return true;
        Serial.println("POWER : appui ignore (etat inconnu ou confirmation en attente)");
      }
    }
    return false;
  }

  bool takeDisplayChanged()
  {
    const bool changed = displayChanged;
    displayChanged = false;
    return changed;
  }

  const char *label()
  {
    if (state == State::UNKNOWN)
      return "PWR ?";
    if (state == State::ON)
      return pending ? "PWR ON*" : "PWR ON";
    return pending ? "PWR OFF*" : "PWR OFF";
  }
} // namespace MousePower
