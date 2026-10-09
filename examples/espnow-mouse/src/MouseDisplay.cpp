#include "MouseDisplay.h"

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <stdio.h>
#include <string.h>

namespace {

// LCD 16x2 avec adaptateur PCF8574 : changer 0x27 en 0x3F si necessaire.
constexpr uint8_t LCD_I2C_ADDRESS = 0x27;
constexpr uint8_t LCD_COLUMNS = 16;
constexpr uint8_t LCD_ROWS = 2;

// Broches proposees pour les cartes de developpement usuelles.
// Verifier la disponibilite sur le modele exact avant de cabler.
#if defined(CONFIG_IDF_TARGET_ESP32S3)
constexpr int LCD_SDA_PIN = 8;
constexpr int LCD_SCL_PIN = 9;
#else
constexpr int LCD_SDA_PIN = 21;
constexpr int LCD_SCL_PIN = 22;
#endif

LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLUMNS, LCD_ROWS);
bool lcdPresent = false;
char previousLine0[17] = {};
char previousLine1[17] = {};

void pad16(char *line) {
  const size_t n = strlen(line);
  for (size_t i = n; i < LCD_COLUMNS; ++i) line[i] = ' ';
  line[LCD_COLUMNS] = '\0';
}

void writeChangedLine(uint8_t row, const char *text, char *previous) {
  if (strcmp(text, previous) == 0) return;
  lcd.setCursor(0, row);
  lcd.print(text);
  strcpy(previous, text);
}

} // namespace

namespace MouseDisplay {

void begin() {
  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);

  Wire.beginTransmission(LCD_I2C_ADDRESS);
  if (Wire.endTransmission() != 0) {
    Serial.printf("LCD absent a l'adresse 0x%02X (SDA=%d, SCL=%d)\n",
                  LCD_I2C_ADDRESS, LCD_SDA_PIN, LCD_SCL_PIN);
    Serial.println("La souris reste utilisable par le moniteur serie.");
    return;
  }

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcdPresent = true;
  Serial.printf("LCD 16x2 detecte : 0x%02X, SDA=%d, SCL=%d\n",
                LCD_I2C_ADDRESS, LCD_SDA_PIN, LCD_SCL_PIN);
}

void show(const MouseState &state) {
  if (!lcdPresent) return;

  char line0[17] = {};
  char line1[17] = {};

  // 15 caracteres : "LOCO 6400  10>>", avec adresses jusqu'a 10239.
  snprintf(line0, sizeof(line0), "LOCO%5u %3u%s",
           static_cast<unsigned>(state.address),
           static_cast<unsigned>(state.speed),
           state.forward ? ">>" : "<<");
  pad16(line0);

  // Affiche uniquement les fonctions actives (simulation locale F0..F3).
  snprintf(line1, sizeof(line1), "%-3s%-3s%-3s%-3s",
           state.functions[0] ? "F0" : "",
           state.functions[1] ? "F1" : "",
           state.functions[2] ? "F2" : "",
           state.functions[3] ? "F3" : "");
  pad16(line1);

  writeChangedLine(0, line0, previousLine0);
  writeChangedLine(1, line1, previousLine1);
}

} // namespace MouseDisplay
