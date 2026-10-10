/*
 * Souris ESP-NOW expérimentale : commandes DCC-EX via moniteur série.
 * ESP32 ou ESP32-S3, Arduino / PlatformIO.
 * Le canal et la MAC de la passerelle sont configurés ci-dessous.
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
#include "LocoManager.h"

static void redrawDisplay();
static void sendCommand(String line);
static LocoManager locoManager;
static bool encoderThrottleCommand = false;
static bool selectingLoco = false;
static uint8_t pendingSlot = 0;

// Un alias sur l'etat du slot actif. Jamais de copie lors d'une selection.
static MouseState &activeLoco() { return locoManager.current(); }

static void handleKey(uint8_t key) {
  // Position matrice : 0..8 = touches 1..9 ; 9='*', 10='0', 11='#'.
  if (key == 11) {
    if (!selectingLoco) {
      selectingLoco = true;
      pendingSlot = 0;
      Serial.println("Selection loco : # numero #, * annule");
      return;
    }
    if (pendingSlot == 0) {
      Serial.println("Selection : saisir d'abord un numero (1..8)");
      return;
    }
    if (!locoManager.select(pendingSlot)) {
      Serial.printf("Loco %u non configuree : adresse a definir dans LocoManager.cpp\n",
                    pendingSlot);
    } else {
      Serial.printf("Loco %u selectionnee (DCC %u), sans commande de vitesse\n",
                    activeLoco().slot, activeLoco().address);
    }
    selectingLoco = false;
    pendingSlot = 0;
    redrawDisplay();
    return;
  }
  if (selectingLoco) {
    if (key == 9) { // *
      selectingLoco = false;
      pendingSlot = 0;
      Serial.println("Selection annulee");
    } else if (key <= 7) {
      pendingSlot = key + 1;
      Serial.printf("Selection en attente : loco %u, confirmer avec #\n", pendingSlot);
    } else {
      Serial.println("Selection : seuls les numeros 1..8 sont disponibles");
    }
    return;
  }
  if (key <= 7) {
    const bool newState = !activeLoco().functions[key];
    Serial.printf("Clavier : F%u %s demande\n", key, newState ? "ON" : "OFF");
    sendCommand("f" + String(key) + (newState ? " on" : " off"));
  }
}



static void redrawDisplay() {
  MouseDisplay::show(activeLoco(), MouseBattery::reading());
}

static void updateLocalState(const String &command) {
  if (command == "<1>" || command == "<0>") return;
  unsigned addr = 0, speed = 0, dir = 0;
  if (sscanf(command.c_str(), "<t %u %u %u>", &addr, &speed, &dir) == 3 &&
      addr >= 1 && addr <= 10239 && speed <= 126 && dir <= 1) {
    MouseState *target = locoManager.findByAddress(static_cast<uint16_t>(addr));
    if (target == nullptr) {
      Serial.printf("Commande DCC %u : adresse absente de la liste, affichage inchange\n", addr);
      return;
    }
    if (!encoderThrottleCommand) {
      // Les commandes serie <t> restent en vitesse DCC-EX (0..126).
      // La souris affiche l'echelle commune 0..100.
      target->speed = speed == 0 ? 0 :
          static_cast<uint8_t>(((speed - 1) * 100U + 62U) / 125U);
    }
    target->forward = (dir == 1);
    if (target == &activeLoco()) redrawDisplay();
  }
}

// Les commandes f0 on/off ... f7 on/off sont analysees par sendCommand.
// Elles doivent etre transmises a la passerelle avant la mise a jour OLED.
static bool parseFunctionCommand(const String &line,
                                 uint8_t &number, bool &enabled) {
  if (line.length() < 5 || line[0] != 'f' ||
      line[1] < '0' || line[1] > '7' || line[2] != ' ') {
    return false;
  }
  const String action = line.substring(3);
  if (action != "on" && action != "off") return false;
  number = static_cast<uint8_t>(line[1] - '0');
  enabled = (action == "on");
  return true;
}


constexpr uint8_t ESPNOW_CHANNEL = 6;

// Remplacer par la MAC STA affichée par la passerelle.
constexpr uint8_t GATEWAY_MAC[6] = {0, 0, 0, 0, 0, 0};

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
    // Protocole prive souris/passerelle, traite par DccExProtocol.cpp.
    line = "<f " + String(activeLoco().address) + " " +
           String(functionNumber) + " " + String(functionEnabled ? 1 : 0) + ">";
  } else if (line == "on") line = "<1>";
  else if (line == "off") line = "<0>";
  else if (line.startsWith("t ")) line = "<" + line + ">";

  if (!line.startsWith("<") || !line.endsWith(">") || line.length() > 64) {
    Serial.println("Syntaxe : f0 on | f0 off | on | off | t 6400 10 1");
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
    // Etat local optimiste : ESP_OK indique la mise en file radio,
    // PAS la reception par la passerelle ni l'execution XpressNet.
    activeLoco().functions[functionNumber] = functionEnabled;
    redrawDisplay();
    Serial.printf("F%u : %s (emission demandee, sans acquittement)\n",
                  functionNumber, functionEnabled ? "ON" : "OFF");
  } else {
    updateLocalState(line);
  }
}


void setup() {
  Serial.begin(115200);
  delay(1000);
  locoManager.begin();
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
  peer.encrypt = false; // Prototype : liaison non chiffree

  if (gatewayConfigured()) {
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
  }

  Serial.println("\n=== SOURIS ESP-NOW (TEST) ===");
  printStaMac("MAC souris");
  Serial.printf("Canal      : %u\n", ESPNOW_CHANNEL);
  Serial.println("Saisir : on | off | t 6400 10 1 | t 6400 0 1");
  Serial.println("Terminer chaque commande par Entree.");
  Serial.println("Fonctions ESP-NOW : f0 on | f0 off | ... | f7 on | f7 off");
  Serial.println("Encodeur : rotation=vitesse, appui en marche=STOP, appui a zero=inversion");
  Serial.println("E-STOP : GPIO32, arret immediat de la locomotive");
}

// Les commandes de l'encodeur restent en 0..100 sur la souris.
// Conversion provisoire en 1..126 DCC-EX pour la passerelle existante.
static uint8_t speedToDccEx(uint8_t speed) {
  if (speed == 0) return 0;
  return static_cast<uint8_t>((static_cast<uint32_t>(speed) * 125 + 50) / 100 + 1);
}

static void sendThrottleFromEncoder() {
  String command = "<t " + String(mouseState.address) + " " +
                   String(speedToDccEx(activeLoco().speed)) + " " +
                   String(activeLoco().forward ? 1 : 0) + ">";
  encoderThrottleCommand = true;
  sendCommand(command);
  encoderThrottleCommand = false;
}

static void sendEmergencyStop() {
  activeLoco().speed = 0;
  redrawDisplay();
  const String command = "<t " + String(mouseState.address) + " -1 " +
                         String(mouseState.forward ? 1 : 0) + ">";
  Serial.println("ARRET D'URGENCE : vitesse XpressNet 1");
  sendCommand(command);
}

static void handleEncoderEvent(const MouseEncoder::Event &event) {
  if (event.pressed) {
    if (activeLoco().speed != 0) {
      activeLoco().speed = 0;
      Serial.println("Encodeur : STOP normal, sens conserve");
    } else {
      activeLoco().forward = !activeLoco().forward;
      Serial.println("Encodeur : inversion du sens a vitesse 0");
    }
    redrawDisplay();
    sendThrottleFromEncoder();
  }

  if (event.step != 0) {
    const uint8_t speed = MouseEncoder::nextSpeed(activeLoco().speed, event.step);
    if (speed != activeLoco().speed) {
      activeLoco().speed = speed;
      redrawDisplay();
      Serial.printf("Encodeur : vitesse=%u sens=%s\n",
                    activeLoco().speed, activeLoco().forward ? ">>" : "<<");
      sendThrottleFromEncoder();
    }
  }
}

void loop() {
  const bool batteryUpdated = MouseBattery::poll();
  const int8_t key = MouseKeypad::poll();
  const bool emergencyPressed = MouseEmergencyStop::poll();
  // L'arret d'urgence reste prioritaire, meme pendant la selection.
  if (!emergencyPressed && !MouseEmergencyStop::isHeld() && key >= 0) {
    handleKey(static_cast<uint8_t>(key));
  }
  // Toujours lire l'encodeur pour ne pas accumuler de transitions.
  const MouseEncoder::Event encoderEvent = MouseEncoder::poll();

  if (emergencyPressed) {
    sendEmergencyStop();
  } else if (!MouseEmergencyStop::isHeld() && !selectingLoco) {
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
  // Actualiser la jauge et l'alerte batterie sans ralentir l'encodeur.
  static uint32_t lastBatteryDrawMs = 0;
  const BatteryReading battery = MouseBattery::reading();
  const uint32_t now = millis();
  if (batteryUpdated ||
      (battery.low && now - lastBatteryDrawMs >= 500)) {
    redrawDisplay();
    lastBatteryDrawMs = now;
    if (batteryUpdated) {
      Serial.printf("Batterie : %.3f V - %u %%\n",
                    battery.voltage, static_cast<unsigned>(battery.percent));
    }
  }
  delay(5);
}
