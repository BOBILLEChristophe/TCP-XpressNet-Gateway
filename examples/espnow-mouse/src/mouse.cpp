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
#include "MouseEncoder.h"

static MouseState mouseState;

static bool encoderThrottleCommand = false;

static void updateLocalState(const String &command) {
  if (command == "<1>" || command == "<0>") return;
  unsigned addr = 0, speed = 0, dir = 0;
  if (sscanf(command.c_str(), "<t %u %u %u>", &addr, &speed, &dir) == 3 &&
      addr >= 1 && addr <= 10239 && speed <= 126 && dir <= 1) {
    mouseState.address = static_cast<uint16_t>(addr);
    if (!encoderThrottleCommand) {
      // Les commandes serie <t> restent en vitesse DCC-EX (0..126).
      // La souris affiche l'echelle commune 0..100.
      mouseState.speed = speed == 0 ? 0 :
          static_cast<uint8_t>(((speed - 1) * 100U + 62U) / 125U);
    }
    mouseState.forward = (dir == 1);
    MouseDisplay::show(mouseState);
  }
}

static bool handleLocalDisplayCommand(String line) {
  line.trim();
  // Les fonctions ne sont PAS encore envoyees a la passerelle.
  if (line.length() == 5 && line[0] == 'f' &&
      line[1] >= '0' && line[1] <= '4' && line[2] == ' ' &&
      (line.substring(3) == "on" || line.substring(3) == "off")) {
    const uint8_t n = line[1] - '0';
    mouseState.functions[n] = (line.substring(3) == "on");
    MouseDisplay::show(mouseState);
    Serial.printf("F%u : %s (affichage local seulement)\n", n,
                  mouseState.functions[n] ? "ON" : "OFF");
    return true;
  }
  return false;
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
  if (handleLocalDisplayCommand(line)) return;

  if (line == "on") line = "<1>";
  else if (line == "off") line = "<0>";
  else if (line.startsWith("t ")) line = "<" + line + ">";

  if (!line.startsWith("<") || !line.endsWith(">") || line.length() > 64) {
    Serial.println("Syntaxe : on | off | t 6400 10 1 | <t 6400 10 1>");
    return;
  }
  if (!gatewayConfigured()) {
    Serial.println("Configurer GATEWAY_MAC dans mouse.cpp");
    return;
  }

  esp_err_t err = esp_now_send(GATEWAY_MAC,
                              reinterpret_cast<const uint8_t *>(line.c_str()),
                              line.length());
  if (err == ESP_OK) {
    Serial.printf("Emission demandee : %s\n", line.c_str());
    updateLocalState(line);
  } else {
    Serial.printf("Erreur esp_now_send : %d\n", static_cast<int>(err));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  MouseEncoder::begin();
  MouseDisplay::begin();
  MouseDisplay::show(mouseState);
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
  Serial.println("Affichage local : f0 on | f0 off | ... | f4 on | f4 off");
  Serial.println("Encodeur : rotation=vitesse, appui en marche=STOP, appui a zero=inversion");
}

// Les commandes de l'encodeur restent en 0..100 sur la souris.
// Conversion provisoire en 1..126 DCC-EX pour la passerelle existante.
static uint8_t speedToDccEx(uint8_t speed) {
  if (speed == 0) return 0;
  return static_cast<uint8_t>((static_cast<uint32_t>(speed) * 125 + 50) / 100 + 1);
}

static void sendThrottleFromEncoder() {
  String command = "<t " + String(mouseState.address) + " " +
                   String(speedToDccEx(mouseState.speed)) + " " +
                   String(mouseState.forward ? 1 : 0) + ">";
  encoderThrottleCommand = true;
  sendCommand(command);
  encoderThrottleCommand = false;
}

static void handleEncoderEvent(const MouseEncoder::Event &event) {
  if (event.pressed) {
    if (mouseState.speed != 0) {
      mouseState.speed = 0;
      Serial.println("Encodeur : STOP normal, sens conserve");
    } else {
      mouseState.forward = !mouseState.forward;
      Serial.println("Encodeur : inversion du sens a vitesse 0");
    }
    MouseDisplay::show(mouseState);
    sendThrottleFromEncoder();
  }

  if (event.step != 0) {
    const uint8_t speed = MouseEncoder::nextSpeed(mouseState.speed, event.step);
    if (speed != mouseState.speed) {
      mouseState.speed = speed;
      MouseDisplay::show(mouseState);
      Serial.printf("Encodeur : vitesse=%u sens=%s\n",
                    mouseState.speed, mouseState.forward ? ">>" : "<<");
      sendThrottleFromEncoder();
    }
  }
}

void loop() {
  handleEncoderEvent(MouseEncoder::poll());
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
  delay(5);
}
