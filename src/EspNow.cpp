#include "EspNow.h"

#include "Config.h"
#include "DccExProtocol.h"
#include "XpressNet.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace {

constexpr size_t MAX_ESPNOW_COMMAND = 64;
constexpr UBaseType_t RX_QUEUE_LENGTH = 16;

struct ReceivedCommand {
  uint8_t source[6];
  uint8_t length;
  char bytes[MAX_ESPNOW_COMMAND];
};

QueueHandle_t rxQueue = nullptr;
volatile uint32_t receivedCount = 0;
volatile uint32_t invalidCount = 0;
volatile uint32_t unauthorizedCount = 0;
volatile uint32_t droppedCount = 0;
volatile uint32_t powerTxCount = 0;
volatile uint32_t powerTxErrors = 0;
constexpr uint32_t POWER_HEARTBEAT_MS = 1500;

void printStaMac(const char *label) {
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

bool macIsConfigured(const uint8_t *mac) {
  for (size_t i = 0; i < 6; ++i) {
    if (mac[i] != 0) return true;
  }
  return false;
}

bool isAllowedMouse(const uint8_t *mac) {
  return macIsConfigured(ESPNOW_ALLOWED_MOUSE_MAC) &&
         memcmp(mac, ESPNOW_ALLOWED_MOUSE_MAC, 6) == 0;
}

void onEspNowReceive(const esp_now_recv_info_t *info,
                     const uint8_t *data, int length) {
  // Callback Wi-Fi : aucune opération bloquante, aucun décodage DCC-EX.
  if (info == nullptr || info->src_addr == nullptr || data == nullptr)
    return;

  if (!isAllowedMouse(info->src_addr)) {
    ++unauthorizedCount;
    return;
  }

  if (length < 3 || length > static_cast<int>(MAX_ESPNOW_COMMAND) ||
      data[0] != '<' || data[length - 1] != '>') {
    ++invalidCount;
    return;
  }

  ReceivedCommand command = {};
  memcpy(command.source, info->src_addr, sizeof(command.source));
  command.length = static_cast<uint8_t>(length);
  memcpy(command.bytes, data, static_cast<size_t>(length));

  if (xQueueSend(rxQueue, &command, 0) != pdTRUE) {
    ++droppedCount;
  }
}

void EspNowTask(void *parameter) {
  ReceivedCommand command;
  int lastSentPowerState = -1;
  uint32_t lastPowerSentMs = 0;
  uint32_t lastAttemptMs = 0;

  for (;;) {
    // Une attente bornee laisse le temps de traiter les commandes recues,
    // tout en surveillant les changements POWER de la LZV100.
    if (xQueueReceive(rxQueue, &command, pdMS_TO_TICKS(50)) == pdTRUE) {
      ++receivedCount;
      Serial.printf("ESP-NOW RX %02X:%02X:%02X:%02X:%02X:%02X : %.*s\n",
                    command.source[0], command.source[1], command.source[2],
                    command.source[3], command.source[4], command.source[5],
                    static_cast<int>(command.length), command.bytes);

      // En mode ESP-NOW, la tache TCP n'est pas lancee :
      // le parseur DCC-EX n'a qu'un seul producteur.
      for (size_t i = 0; i < command.length; ++i) {
        processDccExByte(static_cast<uint8_t>(command.bytes[i]));
      }
    }

    if (!macIsConfigured(ESPNOW_ALLOWED_MOUSE_MAC)) continue;
    const int powerState = getXpressNetPowerState();
    if (powerState < 0) continue; // aucun etat LZV100 encore annonce

    const uint32_t now = millis();
    const bool changed = powerState != lastSentPowerState;
    const bool heartbeat = now - lastPowerSentMs >= POWER_HEARTBEAT_MS;
    if (!(changed || heartbeat) || now - lastAttemptMs < 200) continue;

    lastAttemptMs = now;
    const char *reply = powerState == 1 ? "<p1>" : "<p0>";
    const esp_err_t err = esp_now_send(
        ESPNOW_ALLOWED_MOUSE_MAC,
        reinterpret_cast<const uint8_t *>(reply), strlen(reply));
    if (err == ESP_OK) {
      ++powerTxCount;
      lastSentPowerState = powerState;
      lastPowerSentMs = now;
      if (changed) Serial.printf("ESP-NOW POWER -> souris : %s\n", reply);
    } else {
      ++powerTxErrors;
      Serial.printf("ESP-NOW POWER : erreur d'emission %d\n", (int)err);
    }
  }
}

} // namespace

void initEspNow() {
  Serial.println("Initialisation ESP-NOW (mode autonome)");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  printStaMac("MAC passerelle");
  Serial.printf("Canal ESP-NOW  : %u\n", ESPNOW_CHANNEL);

  if (!macIsConfigured(ESPNOW_ALLOWED_MOUSE_MAC)) {
    Serial.println("ATTENTION : renseigner ESPNOW_ALLOWED_MOUSE_MAC dans Config.h");
    Serial.println("Aucune commande radio ne sera acceptee avant configuration.");
  }

  ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
  ESP_ERROR_CHECK(esp_now_init());

  rxQueue = xQueueCreate(RX_QUEUE_LENGTH, sizeof(ReceivedCommand));
  if (rxQueue == nullptr) {
    Serial.println("ERREUR : impossible de creer la FIFO ESP-NOW");
    while (true) delay(1000);
  }

  ESP_ERROR_CHECK(esp_now_register_recv_cb(onEspNowReceive));

  // ESP-NOW exige l'enregistrement du pair pour les emissions unicast.
  if (macIsConfigured(ESPNOW_ALLOWED_MOUSE_MAC)) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, ESPNOW_ALLOWED_MOUSE_MAC, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false; // Prototype de test non chiffre
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
  }

  if (xTaskCreate(EspNowTask, "EspNowTask", 4096, nullptr, 1, nullptr)
      != pdPASS) {
    Serial.println("ERREUR : creation EspNowTask");
    while (true) delay(1000);
  }

  Serial.println("ESP-NOW pret (commandes RX + retour POWER TX).");
}

void printEspNowStatus() {
  Serial.println("----------- ESP-NOW -----------");
  Serial.printf("Canal          : %u\n", ESPNOW_CHANNEL);
  printStaMac("MAC locale    ");
  Serial.printf("Souris autor.  : %02X:%02X:%02X:%02X:%02X:%02X\n",
                ESPNOW_ALLOWED_MOUSE_MAC[0], ESPNOW_ALLOWED_MOUSE_MAC[1],
                ESPNOW_ALLOWED_MOUSE_MAC[2], ESPNOW_ALLOWED_MOUSE_MAC[3],
                ESPNOW_ALLOWED_MOUSE_MAC[4], ESPNOW_ALLOWED_MOUSE_MAC[5]);
  Serial.printf("Messages recus : %lu\n", (unsigned long)receivedCount);
  Serial.printf("Non autorises  : %lu\n", (unsigned long)unauthorizedCount);
  Serial.printf("Invalides      : %lu\n", (unsigned long)invalidCount);
  Serial.printf("FIFO saturee   : %lu\n", (unsigned long)droppedCount);
  Serial.printf("POWER emis     : %lu\n", (unsigned long)powerTxCount);
  Serial.printf("POWER erreurs  : %lu\n", (unsigned long)powerTxErrors);
  if (rxQueue != nullptr)
    Serial.printf("FIFO attente   : %u / %u\n",
                  (unsigned)uxQueueMessagesWaiting(rxQueue),
                  (unsigned)RX_QUEUE_LENGTH);
}
