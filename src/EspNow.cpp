#include "EspNow.h"

#include "Config.h"
#include "DccExProtocol.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
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
  for (;;) {
    if (xQueueReceive(rxQueue, &command, portMAX_DELAY) != pdTRUE)
      continue;

    ++receivedCount;
    Serial.printf("ESP-NOW RX %02X:%02X:%02X:%02X:%02X:%02X : %.*s\n",
                  command.source[0], command.source[1], command.source[2],
                  command.source[3], command.source[4], command.source[5],
                  static_cast<int>(command.length), command.bytes);

    // Le parseur actuel est global : un seul producteur l'appelle.
    // En mode ESP-NOW, NetworkTask (TCP) n'est jamais démarrée.
    // Les commandes sont mises en file ENTIÈRES et traitées séquentiellement.
    for (size_t i = 0; i < command.length; ++i) {
      processDccExByte(static_cast<uint8_t>(command.bytes[i]));
    }
  }
}

} // namespace

void initEspNow() {
  Serial.println("Initialisation ESP-NOW (mode autonome)");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.printf("MAC passerelle : %s\n", WiFi.macAddress().c_str());
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

  if (xTaskCreate(EspNowTask, "EspNowTask", 4096, nullptr, 1, nullptr)
      != pdPASS) {
    Serial.println("ERREUR : creation EspNowTask");
    while (true) delay(1000);
  }

  Serial.println("ESP-NOW pret (reception uniquement).");
}

void printEspNowStatus() {
  Serial.println("----------- ESP-NOW -----------");
  Serial.printf("Canal          : %u\n", ESPNOW_CHANNEL);
  Serial.printf("MAC locale     : %s\n", WiFi.macAddress().c_str());
  Serial.printf("Souris autor.  : %02X:%02X:%02X:%02X:%02X:%02X\n",
                ESPNOW_ALLOWED_MOUSE_MAC[0], ESPNOW_ALLOWED_MOUSE_MAC[1],
                ESPNOW_ALLOWED_MOUSE_MAC[2], ESPNOW_ALLOWED_MOUSE_MAC[3],
                ESPNOW_ALLOWED_MOUSE_MAC[4], ESPNOW_ALLOWED_MOUSE_MAC[5]);
  Serial.printf("Messages recus : %lu\n", (unsigned long)receivedCount);
  Serial.printf("Non autorises  : %lu\n", (unsigned long)unauthorizedCount);
  Serial.printf("Invalides      : %lu\n", (unsigned long)invalidCount);
  Serial.printf("FIFO saturee   : %lu\n", (unsigned long)droppedCount);
  if (rxQueue != nullptr)
    Serial.printf("FIFO attente   : %u / %u\n",
                  (unsigned)uxQueueMessagesWaiting(rxQueue),
                  (unsigned)RX_QUEUE_LENGTH);
}
