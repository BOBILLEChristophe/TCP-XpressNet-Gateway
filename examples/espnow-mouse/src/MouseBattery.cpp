#include "MouseBattery.h"

namespace {
constexpr uint8_t BATTERY_PIN = 34;       // ADC1 : compatible Wi-Fi / ESP-NOW
constexpr float DIVIDER_RATIO = 2.0f;     // 100 kohms / 100 kohms
constexpr uint32_t SAMPLE_INTERVAL_MS = 2000;
constexpr uint32_t BETWEEN_SAMPLES_MS = 2;
constexpr uint8_t SAMPLE_COUNT = 32;
constexpr uint8_t LOW_BATTERY_PERCENT = 30;

struct BatteryPoint {
  float voltage;
  uint8_t percent;
};

// Estimation indicative de charge Li-ion 1S (depend de la cellule et de la charge).
constexpr BatteryPoint curve[] = {
    {3.30f, 0}, {3.50f, 10}, {3.60f, 20}, {3.70f, 30},
    {3.80f, 45}, {3.90f, 65}, {4.00f, 80}, {4.20f, 100}};

BatteryReading current;
bool initialized = false;
uint32_t lastCompletedMs = 0;
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

void completeSample(uint32_t now) {
  const float measured =
      (sumMilliVolts / static_cast<float>(SAMPLE_COUNT)) *
      DIVIDER_RATIO / 1000.0f;

  // Filtre doux : evite que le pourcentage oscille sur l'OLED.
  current.voltage = initialized ? (0.75f * current.voltage + 0.25f * measured)
                                : measured;
  current.percent = voltageToPercent(current.voltage);
  current.low = current.percent <= LOW_BATTERY_PERCENT;
  initialized = true;
  lastCompletedMs = now;
  sampleCount = 0;
  sumMilliVolts = 0;
}
}  // namespace

void MouseBattery::begin() {
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_PIN, ADC_11db);
  current = BatteryReading{};
  initialized = false;
  sampleCount = 0;
  sumMilliVolts = 0;
  // Demarre immediatement une acquisition sans bloquer l'encodeur ni l'E-STOP.
  lastCompletedMs = millis() - SAMPLE_INTERVAL_MS;
  lastSampleMs = millis() - BETWEEN_SAMPLES_MS;
}

bool MouseBattery::poll() {
  const uint32_t now = millis();

  if (sampleCount == 0 &&
      initialized && now - lastCompletedMs < SAMPLE_INTERVAL_MS) {
    return false;
  }
  if (now - lastSampleMs < BETWEEN_SAMPLES_MS) return false;

  lastSampleMs = now;
  sumMilliVolts += analogReadMilliVolts(BATTERY_PIN);
  ++sampleCount;

  if (sampleCount < SAMPLE_COUNT) return false;
  completeSample(now);
  return true;
}

BatteryReading MouseBattery::reading() {
  return current;
}
