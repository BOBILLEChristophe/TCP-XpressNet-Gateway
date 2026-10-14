#include "MouseDisplay.h"

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

namespace {
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(
    U8G2_R0, U8X8_PIN_NONE, 22, 21);
bool oledPresent = false;
constexpr uint8_t OLED_ADDR = 0x3C;
} // namespace

namespace MouseDisplay {
void begin() {
  Wire.begin(21, 22);
  Wire.setClock(100000);
  delay(100);

  Wire.beginTransmission(OLED_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.println("OLED absent a 0x3C (SDA=21, SCL=22).");
    Serial.println("La souris reste utilisable via le moniteur serie.");
    oledPresent = false;
    return;
  }
  oled.begin();
  oledPresent = true;
  Serial.println("OLED SSD1306 128x64 initialise (SDA=21, SCL=22)");
}

void show(const MouseState &state, const BatteryReading &battery) {
  if (!oledPresent) return;

  oled.clearBuffer();
  oled.setDrawColor(1);

  oled.setFont(u8g2_font_6x10_tf);
  char loco[22];
  snprintf(loco, sizeof(loco), "Loco #%u : %u",
           static_cast<unsigned>(state.slot),
           static_cast<unsigned>(state.address));
  oled.drawStr(0, 10, loco);

  char charge[6];
  snprintf(charge, sizeof(charge), "%u%%",
           static_cast<unsigned>(battery.percent));

  const bool warningOn = battery.low && ((millis() / 1000) % 2 == 0);
  if (warningOn) {
    oled.drawBox(88, 0, 26, 11);
    oled.setDrawColor(0);
  }
  oled.drawStr(89, 10, charge);
  oled.setDrawColor(1);

  oled.drawFrame(115, 1, 11, 8);
  oled.drawBox(126, 3, 2, 4);
  const uint8_t fill =
      (static_cast<uint16_t>(battery.percent) * 7 + 50) / 100;
  if (fill != 0) oled.drawBox(117, 3, fill, 4);

  char speedText[5];
  snprintf(speedText, sizeof(speedText), "%u",
           static_cast<unsigned>(state.speed));
  oled.setFont(u8g2_font_logisoso24_tn);
  const int speedX = (74 - oled.getStrWidth(speedText)) / 2;
  oled.drawStr(speedX > 0 ? speedX : 0, 42, speedText);

  oled.setFont(u8g2_font_9x15B_tf);
  oled.drawStr(82, 35, state.forward ? ">>" : "<<");

  // Dix cases de 12 pixels : F0..F9 sur 128 pixels.
  oled.setFont(u8g2_font_5x7_tf);
  for (uint8_t i = 0; i < 10; ++i) {
    const int x = i * 12;
    if (state.functions[i]) {
      oled.drawBox(x, 51, 11, 12);
      oled.setDrawColor(0);
    } else {
      oled.drawFrame(x, 51, 11, 12);
      oled.setDrawColor(1);
    }
    char label[4];
    snprintf(label, sizeof(label), "F%u", i);
    oled.drawStr(x + 1, 60, label);
    oled.setDrawColor(1);
  }
  oled.sendBuffer();
}
} // namespace MouseDisplay
