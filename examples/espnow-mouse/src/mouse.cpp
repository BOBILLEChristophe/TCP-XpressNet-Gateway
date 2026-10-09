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
  } else {
    Serial.printf("Erreur esp_now_send : %d\n", static_cast<int>(err));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
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
}

void loop() {
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
