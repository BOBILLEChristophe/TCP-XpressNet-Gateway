/*
 * Souris ESP-NOW : clavier 3x4, F0..F9 et selection #1#..#9# / #0#.
 * Chaque locomotive possede un etat local independant.
 * Une case non renseignee (adresse 0) est refusee sans aucune commande.
 * ESP32 classique / PlatformIO.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <string.h>

#include "MouseState.h"
#include "MouseDisplay.h"
#include "MouseBattery.h"
#include "MouseEncoder.h"
#include "MouseEmergencyStop.h"
#include "MouseKeypad.h"

// ------------------------------------------------------------------
// Dix emplacements, numerotes comme sur l'OLED (1 a 10).
// #1# = premiere loco, #2# = deuxieme, ..., #9# = neuvieme.
// #0# = dixieme loco (seul chiffre restant sur le clavier 3x4).
// 6400 est l'adresse de test actuelle. 12/34/56 sont des EXEMPLES.
// Une adresse a 0 signifie emplacement VIDE : selection interdite.
// ------------------------------------------------------------------
constexpr uint8_t LOCO_COUNT = 10;
constexpr uint16_t LOCO_ADDRESSES[LOCO_COUNT] = {
  6400, 12, 34, 56, 0, 0, 0, 0, 0, 0
};

static MouseState locoStates[LOCO_COUNT];
static uint8_t selectedLocoIndex = 0;
static bool encoderThrottleCommand = false;

static MouseState &activeLoco() {
  return locoStates[selectedLocoIndex];
}

static void initializeLocoStates() {
  for (uint8_t i = 0; i < LOCO_COUNT; ++i) {
    locoStates[i].slot = static_cast<uint8_t>(i + 1);
    locoStates[i].address = LOCO_ADDRESSES[i];
    // speed=0, forward=true, functions[]=false par defaut.
  }
  selectedLocoIndex = 0;
}

static void redrawDisplay() {
  MouseDisplay::show(activeLoco(), MouseBattery::reading());
}

static void selectLoco(uint8_t keyDigit) {
  // Le chiffre est le numero OLED, sauf 0 qui designe la loco #10.
  const uint8_t index = keyDigit == 0 ? 9 : static_cast<uint8_t>(keyDigit - 1);
  if (LOCO_ADDRESSES[index] == 0) {
    Serial.printf("Selection #%u# refusee : emplacement non renseigne ; "
                  "loco #%u conservee (adresse %u).\n",
                  static_cast<unsigned>(keyDigit),
                  static_cast<unsigned>(activeLoco().slot),
                  static_cast<unsigned>(activeLoco().address));
    return; // Aucun changement et aucune emission ESP-NOW.
  }

  selectedLocoIndex = index;
  Serial.printf("Selection #%u# : loco #%u, adresse DCC %u, "
                "vitesse locale %u, sens %s.\n",
                static_cast<unsigned>(keyDigit),
                static_cast<unsigned>(activeLoco().slot),
                static_cast<unsigned>(activeLoco().address),
                static_cast<unsigned>(activeLoco().speed),
                activeLoco().forward ? "AV" : "AR");
  redrawDisplay();
  // Changer de loco n'envoie PAS de commande de vitesse ni d'arret.
}

// ------------------------------------------------------------------
// Interpretation de la sequence clavier # chiffre #.
// Hors sequence : chaque chiffre commande directement Fn.
// Dans une sequence incomplete : aucune fonction n'est declenchee.
// * annule ; expiration automatique apres 4 secondes.
// ------------------------------------------------------------------
enum class KeypadMode : uint8_t {
  Normal,
  WaitingDigit,
  WaitingClosingHash
};

static KeypadMode keypadMode = KeypadMode::Normal;
static uint8_t pendingLocoDigit = 0;
static uint32_t keypadLastInputMs = 0;
constexpr uint32_t KEYPAD_SEQUENCE_TIMEOUT_MS = 4000;

static void checkKeypadTimeout() {
  if (keypadMode != KeypadMode::Normal &&
      millis() - keypadLastInputMs >= KEYPAD_SEQUENCE_TIMEOUT_MS) {
    keypadMode = KeypadMode::Normal;
    Serial.println("Selection locomotive : delai depasse, annulee.");
  }
}

static void updateLocalState(const String &command) {
  if (command == "<1>" || command == "<0>") return;
  unsigned addr = 0, speed = 0, dir = 0;
  if (sscanf(command.c_str(), "<t %u %u %u>", &addr, &speed, &dir) == 3 &&
      addr >= 1 && addr <= 10239 && speed <= 126 && dir <= 1) {
    MouseState &state = activeLoco();
    // Ne jamais remplacer l'adresse de l'emplacement selectionne
    // a cause d'une commande saisie au moniteur pour une autre loco.
    if (addr != state.address) return;
    if (!encoderThrottleCommand) {
      state.speed = speed == 0 ? 0 :
          static_cast<uint8_t>(((speed - 1) * 100U + 62U) / 125U);
    }
    state.forward = (dir == 1);
    redrawDisplay();
  }
}

static bool parseFunctionCommand(const String &line,
                                 uint8_t &number, bool &enabled) {
  if (line.length() < 5 || line[0] != 'f' ||
      line[1] < '0' || line[1] > '9' || line[2] != ' ') {
    return false;
  }
  const String action = line.substring(3);
  if (action != "on" && action != "off") return false;
  number = static_cast<uint8_t>(line[1] - '0');
  enabled = (action == "on");
  return true;
}

constexpr uint8_t ESPNOW_CHANNEL = 6;

// Remplacer par la MAC STA affichee par la passerelle.
constexpr uint8_t GATEWAY_MAC[6] = {
  0x30, 0x30, 0xF9, 0x72, 0x82, 0x18
};

static void printStaMac(const char *label) {
  uint8_t mac[6] = {};
  const esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  if (err != ESP_OK) {
    Serial.printf("%s : erreur esp_read_mac (%d)\n", label, (int)err);
    return;
  }
  Serial.printf("%s : %02X:%02X:%02X:%02X:%02X:%02X\n",
                label, mac[0], mac[1], mac[2],
                mac[3], mac[4], mac[5]);
}

static bool gatewayConfigured() {
  for (uint8_t value : GATEWAY_MAC) if (value != 0) return true;
  return false;
}

static void sendCommand(String line) {
  line.trim();
  if (line.isEmpty()) return;

  uint8_t functionNumber = 0;
  bool functionEnabled = false;
  const bool functionCommand =
      parseFunctionCommand(line, functionNumber, functionEnabled);

  if (functionCommand) {
    line = "<f " + String(activeLoco().address) + " " +
           String(functionNumber) + " " + String(functionEnabled ? 1 : 0) + ">";
  } else if (line == "on") line = "<1>";
  else if (line == "off") line = "<0>";
  else if (line.startsWith("t ")) line = "<" + line + ">";

  if (!line.startsWith("<") || !line.endsWith(">") || line.length() > 64) {
    Serial.println("Syntaxe : f0 on | f0 off | ... | f9 on | f9 off | on | off | t 6400 10 1");
    return;
  }
  if (!gatewayConfigured()) {
    Serial.println("Configurer GATEWAY_MAC dans mouse.cpp");
    return;
  }

  const esp_err_t err = esp_now_send(
      GATEWAY_MAC, reinterpret_cast<const uint8_t *>(line.c_str()),
      line.length());

  if (err != ESP_OK) {
    Serial.printf("Erreur esp_now_send : %d\n", static_cast<int>(err));
    return;
  }

  Serial.printf("Emission demandee : %s\n", line.c_str());
  if (functionCommand) {
    // Etat local optimiste : ESP_OK n'est pas un acquittement centrale.
    activeLoco().functions[functionNumber] = functionEnabled;
    redrawDisplay();
    Serial.printf("F%u : %s (emission demandee, sans acquittement)\n",
                  functionNumber, functionEnabled ? "ON" : "OFF");
  } else {
    updateLocalState(line);
  }
}

static void handleKeypadKey(char key) {
  if (key == '\0') return;

  if (keypadMode == KeypadMode::Normal) {
    if (key == '#') {
      keypadMode = KeypadMode::WaitingDigit;
      keypadLastInputMs = millis();
      Serial.println("Selection locomotive : # (attente chiffre puis #)");
      return;
    }
    if (key == '*') return; // Touche reservee pour une fonction future.
    if (key >= '0' && key <= '9') {
      const uint8_t functionNumber = static_cast<uint8_t>(key - '0');
      const bool newState = !activeLoco().functions[functionNumber];
      Serial.printf("Clavier : F%u %s demande (loco #%u, adresse %u)\n",
                    static_cast<unsigned>(functionNumber),
                    newState ? "ON" : "OFF",
                    static_cast<unsigned>(activeLoco().slot),
                    static_cast<unsigned>(activeLoco().address));
      sendCommand("f" + String(functionNumber) +
                  (newState ? " on" : " off"));
    }
    return;
  }

  // Une pression sur * pendant la saisie annule sans rien transmettre.
  if (key == '*') {
    keypadMode = KeypadMode::Normal;
    Serial.println("Selection locomotive annulee par *.");
    return;
  }

  if (keypadMode == KeypadMode::WaitingDigit) {
    if (key >= '0' && key <= '9') {
      pendingLocoDigit = static_cast<uint8_t>(key - '0');
      keypadMode = KeypadMode::WaitingClosingHash;
      keypadLastInputMs = millis();
      Serial.printf("Selection locomotive : #%c (attente #)\n", key);
    } else {
      keypadMode = KeypadMode::Normal;
      Serial.println("Sequence de selection incorrecte, annulee.");
    }
    return;
  }

  // WaitingClosingHash : seul # valide la selection.
  keypadMode = KeypadMode::Normal;
  if (key == '#') {
    selectLoco(pendingLocoDigit);
  } else {
    Serial.println("Sequence de selection incorrecte, annulee (aucune fonction envoyee).");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  initializeLocoStates();
  MouseEncoder::begin();
  MouseEmergencyStop::begin();
  MouseKeypad::begin();
  MouseDisplay::begin();
  MouseBattery::begin();
  redrawDisplay();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
  ESP_ERROR_CHECK(esp_now_init());

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, GATEWAY_MAC, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;

  if (gatewayConfigured()) {
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
  }

  Serial.println("\n=== SOURIS ESP-NOW (TEST) ===");
  printStaMac("MAC souris");
  Serial.printf("Canal      : %u\n", ESPNOW_CHANNEL);
  Serial.println("Clavier : 0..9 = F0..F9 ; #1#..#9# / #0# = selection ; * = annuler");
  for (uint8_t i = 0; i < LOCO_COUNT; ++i) {
    if (LOCO_ADDRESSES[i] != 0) {
      Serial.printf("#%u# -> loco #%u, adresse %u\n",
                    static_cast<unsigned>(i == 9 ? 0 : i + 1),
                    static_cast<unsigned>(i + 1),
                    static_cast<unsigned>(LOCO_ADDRESSES[i]));
    }
  }
  Serial.println("Les autres emplacements sont vides et ne peuvent pas etre selectionnes.");
  Serial.println("Saisir : on | off | t 6400 10 1 | t 6400 0 1");
  Serial.println("Fonctions serie : f0 on | f0 off | ... | f9 on | f9 off");
  Serial.println("Encodeur : rotation=vitesse, appui en marche=STOP, appui a zero=inversion");
  Serial.println("E-STOP : GPIO32, arret immediat de la locomotive selectionnee");
}

static uint8_t speedToDccEx(uint8_t speed) {
  if (speed == 0) return 0;
  return static_cast<uint8_t>((static_cast<uint32_t>(speed) * 125 + 50) / 100 + 1);
}

static void sendThrottleFromEncoder() {
  const MouseState &state = activeLoco();
  String command = "<t " + String(state.address) + " " +
                   String(speedToDccEx(state.speed)) + " " +
                   String(state.forward ? 1 : 0) + ">";
  encoderThrottleCommand = true;
  sendCommand(command);
  encoderThrottleCommand = false;
}

static void sendEmergencyStop() {
  MouseState &state = activeLoco();
  state.speed = 0;
  redrawDisplay();
  const String command = "<t " + String(state.address) + " -1 " +
                         String(state.forward ? 1 : 0) + ">";
  Serial.println("ARRET D'URGENCE : vitesse XpressNet 1 (loco selectionnee)");
  sendCommand(command);
}

static void handleEncoderEvent(const MouseEncoder::Event &event) {
  MouseState &state = activeLoco();
  if (event.pressed) {
    if (state.speed != 0) {
      state.speed = 0;
      Serial.println("Encodeur : STOP normal, sens conserve");
    } else {
      state.forward = !state.forward;
      Serial.println("Encodeur : inversion du sens a vitesse 0");
    }
    redrawDisplay();
    sendThrottleFromEncoder();
  }

  if (event.step != 0) {
    const uint8_t speed = MouseEncoder::nextSpeed(state.speed, event.step);
    if (speed != state.speed) {
      state.speed = speed;
      redrawDisplay();
      Serial.printf("Encodeur : vitesse=%u sens=%s\n",
                    state.speed, state.forward ? ">>" : "<<");
      sendThrottleFromEncoder();
    }
  }
}

void loop() {
  const bool batteryUpdated = MouseBattery::poll();
  checkKeypadTimeout();
  handleKeypadKey(MouseKeypad::poll());

  const bool emergencyPressed = MouseEmergencyStop::poll();
  const MouseEncoder::Event encoderEvent = MouseEncoder::poll();

  if (emergencyPressed) {
    sendEmergencyStop();
  } else if (!MouseEmergencyStop::isHeld()) {
    handleEncoderEvent(encoderEvent);
  }

  static String line;
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      if (!line.isEmpty()) sendCommand(line);
      line = "";
    } else if (line.length() < 80) {
      line += c;
    } else {
      line = "";
      Serial.println("Commande trop longue");
    }
  }

  // La batterie est mesuree toutes les 60 secondes. Seule l'alerte faible
  // impose un rafraichissement OLED supplementaire pour son clignotement.
  static uint32_t lastBatteryDrawMs = 0;
  const BatteryReading battery = MouseBattery::reading();
  const uint32_t now = millis();
  if (batteryUpdated ||
      (battery.low && now - lastBatteryDrawMs >= 1000)) {
    redrawDisplay();
    lastBatteryDrawMs = now;
  }
  if (batteryUpdated) {
    Serial.printf("Batterie : %.3f V - %u %%\n",
                  battery.voltage, static_cast<unsigned>(battery.percent));
  }
  delay(5);
}
