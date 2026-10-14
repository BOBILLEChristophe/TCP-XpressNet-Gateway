#include "MouseBattery.h"

namespace {
constexpr uint8_t BATTERY_PIN = 34;       // ADC1 : compatible Wi-Fi / ESP-NOW
constexpr float DIVIDER_RATIO = 2.0f;     // Diviseur 100 kOhms / 100 kOhms
constexpr uint32_t SAMPLE_INTERVAL_MS = 1000; // Une lecture chaque seconde
constexpr uint8_t SAMPLES_PER_WINDOW = 60;    // Une moyenne chaque minute
constexpr uint8_t STARTUP_SAMPLES = 16;       // Estimation initiale seulement
constexpr uint8_t LOW_BATTERY_PERCENT = 30;
constexpr float FILTER_ALPHA = 0.5f;          // 50 % ancienne, 50 % nouvelle

struct BatteryPoint {
  float voltage;
  uint8_t percent;
};

// Estimation indicative de la charge d'une cellule Li-ion 1S.
constexpr BatteryPoint curve[] = {
    {3.30f, 0}, {3.50f, 10}, {3.60f, 20}, {3.70f, 30},
    {3.80f, 45}, {3.90f, 65}, {4.00f, 80}, {4.20f, 100}};

BatteryReading current;
uint32_t lastSampleMs = 0;
uint32_t sumMilliVolts = 0;
uint8_t sampleCount = 0;

uint8_t voltageToPercent(float voltage) {
  constexpr size_t count = sizeof(curve) / sizeof(curve[0]);
  if (voltage <= curve[0].voltage) return 0;
  if (voltage >= curve[count - 1].voltage) return 100;

  for (size_t i = 1; i < count; ++i) {
    if (voltage <= curve[i].voltage) {
      const BatteryPoint &a = curve[i - 1];
      const BatteryPoint &b = curve[i];
      const float fraction = (voltage - a.voltage) / (b.voltage - a.voltage);
      return static_cast<uint8_t>(
          a.percent + fraction * (b.percent - a.percent) + 0.5f);
    }
  }
  return 0;
}

void updateLowBatteryFlag() {
  // Activation a 30 %, desactivation seulement au-dessus de 33 %.
  current.low = current.low
      ? current.percent <= LOW_BATTERY_PERCENT + 3
      : current.percent <= LOW_BATTERY_PERCENT;
}

void completeWindow() {
  // Moyenne de 60 lectures espacees d'une seconde.
  const float measuredVoltage =
      (sumMilliVolts / static_cast<float>(SAMPLES_PER_WINDOW)) *
      DIVIDER_RATIO / 1000.0f;

  // Lissage de deux moyennes successives (pas de 60 s).
  current.voltage = (1.0f - FILTER_ALPHA) * current.voltage +
                    FILTER_ALPHA * measuredVoltage;

  const uint8_t estimate = voltageToPercent(current.voltage);
  const int difference = static_cast<int>(estimate) -
                         static_cast<int>(current.percent);

  // Hysteresis d'affichage : evite les bascules de +/- 1 point.
  if (difference >= 2 || difference <= -2) {
    current.percent = estimate;
  }
  updateLowBatteryFlag();

  sumMilliVolts = 0;
  sampleCount = 0;
}
} // namespace

void MouseBattery::begin() {
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_PIN, ADC_11db);

  // Valeur initiale disponible des le demarrage, sans attendre 60 s.
  // Seule cette initialisation utilise plusieurs lectures rapprochees.
  uint32_t initialSum = 0;
  for (uint8_t i = 0; i < STARTUP_SAMPLES; ++i) {
    initialSum += analogReadMilliVolts(BATTERY_PIN);
  }
  current.voltage = (initialSum / static_cast<float>(STARTUP_SAMPLES)) *
                    DIVIDER_RATIO / 1000.0f;
  current.percent = voltageToPercent(current.voltage);
  current.low = current.percent <= LOW_BATTERY_PERCENT;

  sumMilliVolts = 0;
  sampleCount = 0;
  lastSampleMs = millis();
}

bool MouseBattery::poll() {
  const uint32_t now = millis();
  if (now - lastSampleMs < SAMPLE_INTERVAL_MS) return false;
  lastSampleMs = now;

  sumMilliVolts += analogReadMilliVolts(BATTERY_PIN);
  ++sampleCount;

  // Aucun rafraichissement batterie avant les 60 lectures.
  if (sampleCount < SAMPLES_PER_WINDOW) return false;

  completeWindow();
  return true; // L'appelant peut rafraichir l'OLED et le moniteur serie.
}

BatteryReading MouseBattery::reading() {
  return current;
}
